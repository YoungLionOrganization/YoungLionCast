# Use direct connections for the first multi-destination broadcast release

A single encoder will serve destinations that accept the same media settings. Each destination will
have an independent direct connection, retry state, and bounded queue, so transport trouble on one
connection cannot block the others. Total upload bandwidth grows with the number of active
destinations. This avoids operating a relay for the first broadcast release. The first
implementation uses video-only H.264 and loopback RTMP to verify shared encoding and independent
connections before platform stream keys and audio are added.
