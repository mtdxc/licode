#include <algorithm>
#include <string.h>
#include "lib/Clock.h"
#include "MediaDefinitions.h"
namespace erizo{
dataPacket::dataPacket(int comp_, const char *data_, int length_, packetType type_, uint64_t received_time_ms_) :
	comp{ comp_ }, length{ length_ }, type{ type_ }, received_time_ms{ received_time_ms_ },
	is_keyframe{ false }, ending_of_layer_frame{ false }
{
	memcpy(data, data_, length_);
}

dataPacket::dataPacket(int comp_, const unsigned char *data_, int length_) :
	comp{ comp_ }, length{ length_ }, type{ VIDEO_PACKET }, received_time_ms{ ClockUtils::msNow() },
	is_keyframe{ false }, ending_of_layer_frame{ false }
{
	memcpy(data, data_, length_);
}

dataPacket::dataPacket(int comp_, const char *data_, int length_, packetType type_) :
	comp{ comp_ }, length{ length_ }, type{ type_ }, received_time_ms{ ClockUtils::msNow() },
	is_keyframe{ false }, ending_of_layer_frame{ false }
{
	memcpy(data, data_, length_);
}

bool dataPacket::belongsToSpatialLayer(int spatial_layer_)
{
	std::vector<int>::iterator item = std::find(compatible_spatial_layers.begin(),
		compatible_spatial_layers.end(),
		spatial_layer_);

	return item != compatible_spatial_layers.end();
}

bool dataPacket::belongsToTemporalLayer(int temporal_layer_)
{
	std::vector<int>::iterator item = std::find(compatible_temporal_layers.begin(),
		compatible_temporal_layers.end(),
		temporal_layer_);

	return item != compatible_temporal_layers.end();
}

bool erizo::MediaSource::isVideoSourceSSRC(uint32_t ssrc)
{
	auto found_ssrc = std::find(video_source_ssrc_list_.begin(), video_source_ssrc_list_.end(), ssrc);
	return (found_ssrc != video_source_ssrc_list_.end());
}
}