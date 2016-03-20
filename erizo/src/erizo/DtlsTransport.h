#ifndef DTLSCONNECTION_H_
#define DTLSCONNECTION_H_

#include <string.h>
#include <boost/thread/mutex.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/scoped_ptr.hpp>
#include "dtls/DtlsSocket.h"
#include "NiceConnection.h"
#include "Transport.h"
#include "logger.h"

namespace erizo {
  class SrtpChannel;
  class Resender;
/*!
 @brief ���������ϵĴ����࣬����һ���ͻ��˵�webrtc����.
 ���͵����ݴ�����������:
 - ice���Ӵ���(NiceConnection)���ռ���ַ��������ѡ��ַ�������ӽ��� 
 - ssl֤�齻������ԿЭ��(DtlsSocketContext),����İ���Ҫ�ش�
 - srtp������Э�̺õ���Կ���мӽ���(SrtpChannel)
 ��ǰʹ�õ��߳��У�
 - ice ���̴����߳�(NiceConnction::init)
 - ice ���ݴ����߳�(getNiceDataLoop)
 - ice �����߳�(WebRtcConnection::sendLoop)����������
 */
  class DtlsTransport : dtls::DtlsReceiver, public Transport {
    DECLARE_LOGGER();
  public:
    DtlsTransport(MediaType med, 
			const std::string &transport_name, 
			bool bundle, bool rtcp_mux, 
			TransportListener *transportListener, 
			const IceConfig& iceConfig, 
			std::string username, std::string password, 
			bool isServer);
    virtual ~DtlsTransport();

    std::string getMyFingerprint();
		
    void connectionStateChanged(IceState newState);
		// implement NiceConnectionListener method
    void onNiceData(unsigned int component_id, char* data, int len, NiceConnection* nice);
    void onCandidate(const CandidateInfo &candidate, NiceConnection *conn);
    void updateIceState(IceState state, NiceConnection *conn);

    void write(char* data, int len);
		// implement DtlsReceiver method
    void writeDtls(dtls::DtlsSocketContext *ctx, 
			const unsigned char* data, unsigned int len);
    void onHandshakeCompleted(dtls::DtlsSocketContext *ctx, 
			std::string clientKey, std::string serverKey, 
			std::string srtp_profile);

    void processLocalSdp(SdpInfo *localSdp_);

  private:
		// ���ͻ�����
    char protectBuf_[5000];
		// ���ջ�����
    char unprotectBuf_[5000];
    boost::mutex writeMutex_,sessionMutex_;
		// ֤�齻������ԿЭ��������(rtp��rtcp��һ������rtcp_mux��û��rtcp)
    boost::shared_ptr<dtls::DtlsSocketContext> dtlsRtp, dtlsRtcp;
		boost::scoped_ptr<Resender> rtpResender, rtcpResender;
		// �ӽ���������(rtp��rtcp��һ�������rtcp_mux��û��rtcp)
    boost::scoped_ptr<SrtpChannel> srtp_, srtcp_;
    bool readyRtp, readyRtcp;

    bool running_, isServer_;
		// ���ݽ����߳�
    boost::thread getNice_Thread_;
    void getNiceDataLoop();
    packetPtr p_;
  };

	// ����dlts�����ط�
  class Resender {
    DECLARE_LOGGER();
  public:
    Resender(boost::shared_ptr<NiceConnection> nice, unsigned int comp, 
			const unsigned char* data, unsigned int len);
    virtual ~Resender();

    void start();
    void cancel();
    int getStatus();
    void resend(const boost::system::error_code& ec);
  private:
    boost::shared_ptr<NiceConnection> nice_;
    unsigned int comp_;
    const unsigned char* data_;
    unsigned int len_;
		/* 
		resend status.
		- 0 init 
		- 1 user cancel
		- -1 reset error
		- 2 resend okay
		*/
    int sent_;
    boost::asio::deadline_timer timer;
  };
}
#endif
