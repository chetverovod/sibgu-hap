#ifndef SIBGU_HAP_DEVICE_IP_TABLE_H
#define SIBGU_HAP_DEVICE_IP_TABLE_H

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

namespace ns3
{

class NodeContainer;

using DeviceIpRow = std::tuple<uint32_t, std::string, uint32_t, std::string, std::string>;

void CollectDeviceIpRows(NodeContainer nodes,
                         const std::string& role,
                         std::vector<DeviceIpRow>& rows);

void PrintDeviceIpTable(const std::vector<DeviceIpRow>& rows);

void SaveDeviceIpTableToFile(const std::vector<DeviceIpRow>& rows, const std::string& outputPath);

} // namespace ns3

#endif // SIBGU_HAP_DEVICE_IP_TABLE_H
