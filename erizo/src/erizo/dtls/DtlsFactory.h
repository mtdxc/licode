#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef DtlsFactory_h
#define DtlsFactory_h

#include <memory>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include "DtlsTimer.h"
#include <openssl/evp.h>
#include "logger.h"
typedef struct x509_st X509;
typedef struct ssl_ctx_st SSL_CTX;
typedef struct evp_pkey_st EVP_PKEY;

namespace dtls
{
class DtlsSocket;
class DtlsSocketContext;

//Not threadsafe. Timers must fire in the same thread as dtls processing.
class DtlsFactory
{
   DECLARE_LOGGER();
   public:
     enum PacketType { rtp, dtls, stun, unknown};

     // Creates a new DtlsSocket to be used as a client
     DtlsSocket* createClient(boost::shared_ptr<DtlsSocketContext> context);

     // Creates a new DtlsSocket to be used as a server
     DtlsSocket* createServer(boost::shared_ptr<DtlsSocketContext> context);

     // Examines the first few bits of a packet to determine its type: rtp, dtls, stun or unknown
     static PacketType demuxPacket(const unsigned char *buf, unsigned int len);

     static DtlsFactory* GetInstance();

     static void Init();
     static void Destory();
     static boost::asio::io_service& service(){ return service_; }
private:
     friend class DtlsSocket;
     // Creates a DTLS SSL Context and enables srtp extension, also sets the private and public key cert
     static X509 *mCert;
     static EVP_PKEY *privkey;

     // @todo move all timer to one thread(current every resend has one thread)
     static boost::asio::io_service service_;
     static boost::scoped_ptr<boost::thread> thread_;
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
