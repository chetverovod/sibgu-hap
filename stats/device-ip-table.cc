#include "device-ip-table.h"

#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace ns3
{

void
CollectDeviceIpRows(NodeContainer nodes, const std::string& role, std::vector<DeviceIpRow>& rows)
{
    for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
        Ptr<Node> node = nodes.Get(i);

        for (uint32_t j = 0; j < node->GetNDevices(); ++j)
        {
            Ptr<NetDevice> dev = node->GetDevice(j);
            std::string typeName = dev->GetInstanceTypeId().GetName();

            if (typeName == "ns3::LoopbackNetDevice")
            {
                continue;
            }

            std::string ipAddr = "N/A";
            Ptr<Ipv4> ipStack = node->GetObject<Ipv4>();

            if (ipStack)
            {
                int32_t interfaceIndex = ipStack->GetInterfaceForDevice(dev);
                if (interfaceIndex >= 0 && ipStack->GetNAddresses(interfaceIndex) > 0)
                {
                    Ipv4InterfaceAddress ifaceAddr = ipStack->GetAddress(interfaceIndex, 0);
                    std::ostringstream ipStream;
                    ipStream << ifaceAddr.GetLocal();
                    ipAddr = ipStream.str();
                }
            }

            rows.emplace_back(node->GetId(), role, j, typeName, ipAddr);
        }
    }
}

void
PrintDeviceIpTable(const std::vector<DeviceIpRow>& rows)
{
    std::cout << std::endl;
    std::cout << "====================================================================================" << std::endl;
    std::cout << "| Node ID | Role | Dev ID | Type                          | IP Address             |"
              << std::endl;
    std::cout << "====================================================================================" << std::endl;
    for (const auto& row : rows)
    {
        std::cout << "| " << std::setw(7) << std::get<0>(row)
                  << " | " << std::setw(4) << std::get<1>(row)
                  << " | " << std::setw(6) << std::get<2>(row)
                  << " | " << std::setw(29) << std::get<3>(row)
                  << " | " << std::setw(22) << std::get<4>(row) << " |" << std::endl;
    }
    std::cout << "====================================================================================" << std::endl;
    std::cout << std::endl;
}

void
SaveDeviceIpTableToFile(const std::vector<DeviceIpRow>& rows, const std::string& outputPath)
{
    std::ofstream out(outputPath, std::ios::out | std::ios::trunc);
    NS_ABORT_MSG_UNLESS(out.is_open(), "Cannot open devices table output file: " << outputPath);

    // Keep syntax close to scatter/stat files: metadata lines start with '%'.
    out << "% output_type: 'OUTPUT_TYPE_TABLE'" << std::endl;
    out << "% source: 'sat-handover-hap'" << std::endl;
    out << "% count: " << rows.size() << std::endl;
    out << "% node_id role dev_id device_type ip_address" << std::endl;
    for (const auto& row : rows)
    {
        out << std::get<0>(row) << " "
            << std::get<1>(row) << " "
            << std::get<2>(row) << " "
            << std::get<3>(row) << " "
            << std::get<4>(row) << std::endl;
    }
}

} // namespace ns3
