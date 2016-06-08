#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef WIN32
#include <WinSock2.h>
#endif
#include <iostream>
#include <cassert>
#include <string.h>

#include "DtlsSocket.h"
#include "DtlsTimer.h"
#include "bf_dwrap.h"

#ifdef WIN32
#include <srtp.h>
#else
#include <srtp/srtp.h>
#endif

using namespace std;
using namespace dtls;

const int SRTP_MASTER_KEY_BASE64_LEN = SRTP_MASTER_KEY_LEN * 4 / 3;

DEFINE_LOGGER(DtlsSocket, "dtls.DtlsSocket");


void SSLInfoCallback(const SSL* s, int where, int ret);
int SSLVerifyCallback(int ok, X509_STORE_CTX* store);

// Our local timers
class dtls::DtlsSocketTimer : public DtlsTimer
{
  public:
      DtlsSocketTimer(unsigned int seq, DtlsSocket *socket): DtlsTimer(seq), mSocket(socket){}
      ~DtlsSocketTimer(){}

      void expired()
      {
         mSocket->expired(this);
      }
  private:
     DtlsSocket *mSocket;
};

int dummy_cb(int d, X509_STORE_CTX *x)
{
   return 1;
}

DtlsSocket::DtlsSocket(DtlsSocketContext* socketContext, enum SocketType type):
   mSocketContext(socketContext),
   mReadTimer(0),
   mSocketType(type),
   mHandshakeCompleted(false)
{
   ELOG_DEBUG("Creating Dtls Socket");
   mSocketContext->setDtlsSocket(this);
   SSL_CTX* mContext= mSocketContext->getSSLContext();
   assert(mContext);
   mSsl=SSL_new(mContext);
   assert(mSsl!=0);
   mSsl->ctx = mContext;
   mSsl->session_ctx = mContext;

   switch(type)
   {
   case Client:
      SSL_set_connect_state(mSsl);
      //SSL_set_mode(mSsl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
      break;
   case Server:
      SSL_set_accept_state(mSsl);
      SSL_set_verify(mSsl, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, dummy_cb);
      break;
   default:
      assert(0);
   }

   BIO* memBIO1 = BIO_new(BIO_s_mem());
   mInBio=BIO_new(BIO_f_dwrap());
   BIO_push(mInBio,memBIO1);

   BIO* memBIO2 = BIO_new(BIO_s_mem());
   mOutBio=BIO_new(BIO_f_dwrap());
   BIO_push(mOutBio,memBIO2);

   SSL_set_bio(mSsl,mInBio,mOutBio);
   SSL_accept(mSsl);
   ELOG_DEBUG("Dtls Socket created");
}

DtlsSocket::~DtlsSocket()
{
  ELOG_DEBUG("Deleting Socket");
  if(mReadTimer) mReadTimer->invalidate();

  ELOG_DEBUG("Invalidated Timer");
   // Properly shutdown the socket and free it - note: this also free's the BIO's
   if (mSsl != NULL) {
    ELOG_DEBUG("SSL Shutdown");
      SSL_shutdown(mSsl);
      SSL_free(mSsl);
      mSsl = NULL;
   }
}

void DtlsSocket::expired(DtlsSocketTimer* timer)
{
   forceRetransmit();
}

void DtlsSocket::startClient()
{
   assert(mSocketType == Client);
   doHandshakeIteration();
}

bool
DtlsSocket::handlePacketMaybe(const unsigned char* bytes, unsigned int len)
{
   DtlsSocketContext::PacketType pType=DtlsSocketContext::demuxPacket(bytes,len);

   if(pType!=DtlsSocketContext::dtls)
      return false;

   BIO_reset(mInBio);
   BIO_reset(mOutBio);

   int r = BIO_write(mInBio,bytes,len);
   assert(r==(int)len);  // Can't happen

   // Note: we must catch any below exceptions--if there are any
   try {
     doHandshakeIteration();
   } catch (...) {
		 return false;
   }
   return true;
}

void DtlsSocket::forceRetransmit()
{
   BIO_reset(mInBio);
   BIO_reset(mOutBio);
   BIO_ctrl(mInBio,BIO_CTRL_DGRAM_SET_RECV_TIMEOUT,0,0);

   doHandshakeIteration();
}

void DtlsSocket::doHandshakeIteration()
{
   boost::mutex::scoped_lock lock(handshakeMutex_);

   if(mHandshakeCompleted)
      return;

   int r=SSL_do_handshake(mSsl);

   // See what was written
   unsigned char *outBioData;
   int outBioLen = BIO_get_mem_data(mOutBio,&outBioData);

   // Now handle handshake errors */
	 int sslerr = SSL_get_error(mSsl, r);
   switch(sslerr)
   {
   case SSL_ERROR_NONE:
      mHandshakeCompleted = true;
      mSocketContext->handshakeCompleted();
			if (mReadTimer){
				mReadTimer->invalidate();
				mReadTimer = 0;
			} 
      break;
   case SSL_ERROR_WANT_READ:
      // There are two cases here:
      // (1) We didn't get enough data. In this case we leave the
      //     timers alone and wait for more packets.
      // (2) We did get a full flight and then handled it, but then
      //     wrote some more message and now we need to flush them
      //     to the network and now reset the timers
      //
      // If data was written then this means we got a complete
      // something or a retransmit so we need to reset the timer
      if(outBioLen)
      {
				 // 这个定时器没用，因为mTimerContext的updateTimer方法没被调用 
         if(mReadTimer) mReadTimer->invalidate();
         mReadTimer=new DtlsSocketTimer(0,this);
         mSocketContext->addTimerToContext(mReadTimer,500);
         mTimerContext->addTimer(mReadTimer,500);
      }
      break;
	 default:
		  {
			  char errbuf[1024] = { 0 };
			  ERR_error_string_n(sslerr, errbuf, sizeof(errbuf));
			  ELOG_ERROR("SSL error %d", sslerr);
			  mSocketContext->handshakeFailed(errbuf);
		  }
      // Note: need to fall through to propagate alerts, if any
      break;
   }

   // If mOutBio is now nonzero-length, then we need to write the
   // data to the network. TODO: warning, MTU issues!
   if(outBioLen)
   {
      mSocketContext->write(outBioData,outBioLen);
   }
}

bool DtlsSocket::getRemoteFingerprint(char *fprint)
{
   X509* x = SSL_get_peer_certificate(mSsl);
   if(!x) // No certificate
      return false;

   computeFingerprint(x,fprint);
   X509_free(x);
   return true;
}

bool DtlsSocket::checkFingerprint(const char* fingerprint, unsigned int len)
{
   char fprint[100];
   if(!getRemoteFingerprint(fprint))
      return false;

   // used to be strncasecmp
   if(strncmp(fprint,fingerprint,len)){
      ELOG_WARN( "Fingerprint mismatch, got %s expecting %s", fprint, fingerprint );
      return false;
   }

   return true;
}

void DtlsSocket::getMyCertFingerprint(char *fingerprint)
{
   mSocketContext->getMyCertFingerprint(fingerprint);
}

SrtpSessionKeys* DtlsSocket::getSrtpSessionKeys()
{
   //TODO: probably an exception candidate
   assert(mHandshakeCompleted);

   SrtpSessionKeys* keys = new SrtpSessionKeys();

   unsigned char material[SRTP_MASTER_KEY_LEN << 1];
   if (!SSL_export_keying_material( mSsl,
      material, sizeof(material),
      "EXTRACTOR-dtls_srtp", 19, NULL, 0, 0))
   {
      return keys;
   }

   size_t offset = 0;

   memcpy(keys->clientMasterKey, &material[offset], SRTP_MASTER_KEY_KEY_LEN);
   offset += SRTP_MASTER_KEY_KEY_LEN;
   memcpy(keys->serverMasterKey, &material[offset], SRTP_MASTER_KEY_KEY_LEN);
   offset += SRTP_MASTER_KEY_KEY_LEN;
   memcpy(keys->clientMasterSalt, &material[offset], SRTP_MASTER_KEY_SALT_LEN);
   offset += SRTP_MASTER_KEY_SALT_LEN;
   memcpy(keys->serverMasterSalt, &material[offset], SRTP_MASTER_KEY_SALT_LEN);
   offset += SRTP_MASTER_KEY_SALT_LEN;
   keys->clientMasterKeyLen = SRTP_MASTER_KEY_KEY_LEN;
   keys->serverMasterKeyLen = SRTP_MASTER_KEY_KEY_LEN;
   keys->clientMasterSaltLen = SRTP_MASTER_KEY_SALT_LEN;
   keys->serverMasterSaltLen = SRTP_MASTER_KEY_SALT_LEN;

   return keys;
}

SRTP_PROTECTION_PROFILE*
DtlsSocket::getSrtpProfile()
{
   //TODO: probably an exception candidate
   assert(mHandshakeCompleted);
   return SSL_get_selected_srtp_profile(mSsl);
}

// Fingerprint is assumed to be long enough
void
DtlsSocket::computeFingerprint(X509 *cert, char *fingerprint)
{
   unsigned char md[EVP_MAX_MD_SIZE];
   int r;
   unsigned int i,n;
	 /* 
	 !slg! TODO - is sha1 vs sha256 supposed to come from DTLS handshake? 
	 fixing to to SHA-256 for compatibility with current web-rtc implementations
	 */
   //r=X509_digest(cert,EVP_sha1(),md,&n);
   r=X509_digest(cert,EVP_sha256(),md,&n);  
   assert(r==1);

   for(i=0;i<n;i++)
   {
      sprintf(fingerprint,"%02X",md[i]);
      fingerprint+=2;

      if(i<(n-1))
         *fingerprint++=':';
      else
         *fingerprint++=0;
   }
}

//TODO: assert(0) into exception, as elsewhere
void
DtlsSocket::createSrtpSessionPolicies(srtp_policy_t& outboundPolicy, srtp_policy_t& inboundPolicy)
{
   assert(mHandshakeCompleted);

   /* we assume that the default profile is in effect, for now */
   srtp_profile_t profile = srtp_profile_aes128_cm_sha1_80;
   int key_len = srtp_profile_get_master_key_length(profile);
   int salt_len = srtp_profile_get_master_salt_length(profile);

   /* get keys from srtp_key and initialize the inbound and outbound sessions */
   uint8_t *client_master_key_and_salt=new uint8_t[SRTP_MAX_KEY_LEN];
   uint8_t *server_master_key_and_salt=new uint8_t[SRTP_MAX_KEY_LEN];
   srtp_policy_t client_policy;
   memset(&client_policy, 0, sizeof(srtp_policy_t));
   client_policy.window_size = 128;
   client_policy.allow_repeat_tx = 1;
   srtp_policy_t server_policy;
   memset(&server_policy, 0, sizeof(srtp_policy_t));
   server_policy.window_size = 128;
   server_policy.allow_repeat_tx = 1;

   SrtpSessionKeys* srtp_key = getSrtpSessionKeys();
   /* set client_write key */
   client_policy.key = client_master_key_and_salt;
   if (srtp_key->clientMasterKeyLen != key_len)
   {
      ELOG_WARN( "error: unexpected client key length" );
      assert(0);
   }
   if (srtp_key->clientMasterSaltLen != salt_len)
   {
      ELOG_WARN( "error: unexpected client salt length" );
      assert(0);
   }

   memcpy(client_master_key_and_salt, srtp_key->clientMasterKey, key_len);
   memcpy(client_master_key_and_salt + key_len, srtp_key->clientMasterSalt, salt_len);

   /* initialize client SRTP policy from profile  */
   err_status_t err = crypto_policy_set_from_profile_for_rtp(&client_policy.rtp, profile);
   if (err) assert(0);

   err = crypto_policy_set_from_profile_for_rtcp(&client_policy.rtcp, profile);
   if (err) assert(0);
   client_policy.next = NULL;

   /* set server_write key */
   server_policy.key = server_master_key_and_salt;

   if (srtp_key->serverMasterKeyLen != key_len)
   {
      ELOG_WARN( "error: unexpected server key length" );
      assert(0);
   }
   if (srtp_key->serverMasterSaltLen != salt_len)
   {
      ELOG_WARN( "error: unexpected salt length" );
      assert(0);
   }

   memcpy(server_master_key_and_salt, srtp_key->serverMasterKey, key_len);
   memcpy(server_master_key_and_salt + key_len, srtp_key->serverMasterSalt, salt_len);

   delete srtp_key;

   /* initialize server SRTP policy from profile  */
   err = crypto_policy_set_from_profile_for_rtp(&server_policy.rtp, profile);
   if (err) assert(0);

   err = crypto_policy_set_from_profile_for_rtcp(&server_policy.rtcp, profile);
   if (err) assert(0);
   server_policy.next = NULL;

   if (mSocketType == Client)
   {
      client_policy.ssrc.type = ssrc_any_outbound;
      outboundPolicy = client_policy;

      server_policy.ssrc.type  = ssrc_any_inbound;
      inboundPolicy = server_policy;
   }
   else
   {
      server_policy.ssrc.type  = ssrc_any_outbound;
      outboundPolicy = server_policy;

      client_policy.ssrc.type = ssrc_any_inbound;
      inboundPolicy = client_policy;
   }
   /* zeroize the input keys (but not the srtp session keys that are in use) */
   //not done...not much of a security whole imho...the lifetime of these seems odd though
   //    memset(client_master_key_and_salt, 0x00, SRTP_MAX_KEY_LEN);
   //    memset(server_master_key_and_salt, 0x00, SRTP_MAX_KEY_LEN);
   //    memset(&srtp_key, 0x00, sizeof(srtp_key));
}

/* ====================================================================

 Copyright (c) 2007-2008, Eric Rescorla and Derek MacDonald
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are
 met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

 3. None of the contributors names may be used to endorse or promote
    products derived from this software without specific prior written
    permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 ==================================================================== */

void dtls::DtlsSocket::setSrtpProfiles(const char *str)
{
  int r = SSL_CTX_set_tlsext_use_srtp(mContext, str);
  assert(r == 0);
}

void dtls::DtlsSocket::setCipherSuites(const char *str)
{
  int r = SSL_CTX_set_cipher_list(mContext, str);
  assert(r == 1);
}