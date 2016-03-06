#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef DtlsSocket_h
#define DtlsSocket_h

#include <memory>
#include <string.h>

#include <openssl/e_os2.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>

#include <boost/thread/mutex.hpp>
#include <boost/shared_ptr.hpp>

#include "logger.h"

const int SRTP_MASTER_KEY_KEY_LEN = 16;
const int SRTP_MASTER_KEY_SALT_LEN = 14;

struct srtp_policy_t;
namespace dtls
{
class DtlsFactory;
class DtlsSocketContext;
class DtlsTimer;
class DtlsSocketTimer;
class DtlsTimerContext;

class SrtpSessionKeys
{
public:
    SrtpSessionKeys() {
        clientMasterKeyLen = 0;
        clientMasterSaltLen = 0;
        serverMasterKeyLen = 0;
        serverMasterSaltLen = 0;
    }
		unsigned char clientMasterKey[SRTP_MASTER_KEY_KEY_LEN];
		int clientMasterKeyLen;
		unsigned char serverMasterKey[SRTP_MASTER_KEY_KEY_LEN];
		int serverMasterKeyLen;
		unsigned char clientMasterSalt[SRTP_MASTER_KEY_SALT_LEN];
		int clientMasterSaltLen;
		unsigned char serverMasterSalt[SRTP_MASTER_KEY_SALT_LEN];
		int serverMasterSaltLen;
};

class DtlsSocket
{
   DECLARE_LOGGER();
   public:
      enum SocketType { Client, Server};
      ~DtlsSocket();

      // Inspects packet to see if it's a DTLS packet, if so continue processing
      bool handlePacketMaybe(const unsigned char* bytes, unsigned int len);

      // Called by DtlSocketTimer when timer expires - causes a retransmission (forceRetransmit)
      void expired(DtlsSocketTimer*);

      // Retrieves the finger print of the certificate presented by the remote party
      bool getRemoteFingerprint(char *fingerprint);

      // Retrieves the finger print of the certificate presented by the remote party and checks
      // it agains the passed in certificate
      bool checkFingerprint(const char* fingerprint, unsigned int len);

      // Retrieves the finger print of our local certificate, same as getMyCertFingerprint from DtlsFactory
      void getMyCertFingerprint(char *fingerprint);

      // For client sockets only - causes a client handshake to start (doHandshakeIteration)
      void startClient();

      // Changes the default SRTP profiles supported (default is: SRTP_AES128_CM_SHA1_80:SRTP_AES128_CM_SHA1_32)
      void setSrtpProfiles(const char *policyStr);

      // Changes the default DTLS Cipher Suites supported
      void setCipherSuites(const char *cipherSuites);

      // Retreives the SRTP session keys from the Dtls session
      SrtpSessionKeys* getSrtpSessionKeys();

      // Utility fn to compute a certificates fingerprint
      static void computeFingerprint(X509 *cert, char *fingerprint);

      // Retrieves the DTLS negotiated SRTP profile - may return 0 if profile selection failed
      SRTP_PROTECTION_PROFILE* getSrtpProfile();

      // Creates SRTP session policies appropriately based on socket type (client vs server) and keys
      // extracted from the DTLS handshake process
      void createSrtpSessionPolicies(srtp_policy_t& outboundPolicy, srtp_policy_t& inboundPolicy);

      DtlsSocketContext* getSocketContext() { return mSocketContext.get(); }

   private:
      friend class DtlsFactory;

      // Causes an immediate handshake iteration to happen, which will retransmit the handshake
      void forceRetransmit();

      // Creates an SSL socket, and if client sets state to connect_state and if server sets state to accept_state.  Sets SSL BIO's.
      DtlsSocket(boost::shared_ptr<DtlsSocketContext> socketContext, enum SocketType);

      // Give CPU cyces to the handshake process - checks current state and acts appropraitely
      void doHandshakeIteration();

      // Internals
      boost::shared_ptr<DtlsSocketContext> mSocketContext;
      DtlsTimer *mReadTimer;  // Timer used during handshake process

      bool InitSSLCtx();
      void ClearSSLCtx();
      SSL_CTX* mContext;
      EVP_MD_CTX* ctx_;
      std::auto_ptr<DtlsTimerContext> mTimerContext;

      // OpenSSL context data
      SSL *mSsl;
      BIO *mInBio;
      BIO *mOutBio;

      SocketType mSocketType;
      bool mHandshakeCompleted;
      boost::mutex handshakeMutex_;
};

class DtlsReceiver
{
public:
      virtual void writeDtls(DtlsSocketContext *ctx, 
				const unsigned char* data, unsigned int len)=0;
      virtual void onHandshakeCompleted(DtlsSocketContext *ctx, 
		    std::string clientKey, std::string serverKey, 
		    std::string srtp_profile) = 0;
};

class DtlsSocketContext
{
   DECLARE_LOGGER();
   public:
      bool started;
      //memory is only valid for duration of callback; must be copied if queueing
      //is required
      DtlsSocketContext();
      virtual ~DtlsSocketContext();

      void start();
			// read for socket
      void read(const unsigned char* data, unsigned int len);
			// write to socket(call DtlsReceiver::writeDtls need retransmite)
      void write(const unsigned char* data, unsigned int len);

			// make onHandshakeCompleted callback
      void handshakeCompleted();
      void handshakeFailed(const char *err);

      void setDtlsReceiver(DtlsReceiver *recv);
      void setDtlsSocket(DtlsSocket *sock) {mSocket = sock;}
      std::string getFingerprint();

   protected:
      DtlsSocket *mSocket;
      DtlsReceiver *receiver;
};

}

#endif
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
