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
 @brief 完整意义上的传输类，代表一个客户端的webrtc连接.
 典型的数据处理流程如下:
 - ice连接处理(NiceConnection)：收集地址，交换候选地址，打洞连接建立 
 - ssl证书交换和密钥协商(DtlsSocketContext),这里的包需要重传
 - srtp再利用协商好的密钥进行加解密(SrtpChannel)
 当前使用的线程有：
 - ice 流程处理线程(NiceConnction::init)
 - ice 数据处理线程(getNiceDataLoop)
 - ice 发送线程(WebRtcConnection::sendLoop)，负责流控
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
		// 发送缓冲区
    char protectBuf_[5000];
		// 接收缓冲区
    char unprotectBuf_[5000];
    boost::mutex writeMutex_,sessionMutex_;
		// 证书交换和密钥协商上下文(rtp和rtcp各一个，如rtcp_mux则没有rtcp)
    boost::shared_ptr<dtls::DtlsSocketContext> dtlsRtp, dtlsRtcp;
		boost::scoped_ptr<Resender> rtpResender, rtcpResender;
		// 加解密上下文(rtp和rtcp各一个，如果rtcp_mux则没有rtcp)
    boost::scoped_ptr<SrtpChannel> srtp_, srtcp_;
    bool readyRtp, readyRtcp;

    bool running_, isServer_;
		// 数据接收线程
    boost::thread getNice_Thread_;
    void getNiceDataLoop();
    packetPtr p_;
  };

	// 负责dlts包的重发
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
