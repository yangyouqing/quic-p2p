# quic-p2p
quic based media transport,  p2p hole punching is supported
嵌入式专用p2p音视频传输方案

性能指标:
p2p直连目标:   95%以上(可穿透部分对称NAT)
首帧加载速度:  500ms以内 (信令/音频/视频共用一个quic通道, 先通后优, 信令通即视频通)
webrtc特征:    支持 pacer, jitter buffer, ice, 带宽预测, 自动码率
连接迁移:      支持

