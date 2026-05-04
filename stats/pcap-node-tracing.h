#ifndef SIBGU_HAP_PCAP_NODE_TRACING_H
#define SIBGU_HAP_PCAP_NODE_TRACING_H

#include "ns3/network-module.h"

#include <string>

namespace ns3
{

void EnablePcapForNodeContainer(NodeContainer nodes,
                                const std::string& prefix,
                                const std::string& outputDir,
                                const std::string& role,
                                bool enableHexDump);

} // namespace ns3

#endif // SIBGU_HAP_PCAP_NODE_TRACING_H
