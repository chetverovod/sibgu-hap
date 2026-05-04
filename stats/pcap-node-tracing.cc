#include "pcap-node-tracing.h"

#include "ns3/core-module.h"
#include "ns3/satellite-module.h"
#include "ns3/trace-helper.h"
#include "ns3/system-path.h"

#include <iomanip>
#include <sstream>

namespace ns3
{

static void
PcapTxSink(Ptr<PcapFileWrapper> file, Ptr<const Packet> packet)
{
    file->Write(Simulator::Now(), packet);
}

static void
PcapRxSink(Ptr<PcapFileWrapper> file, Ptr<const Packet> packet, const Address& from)
{
    file->Write(Simulator::Now(), packet);
}

static void
HexDumpTx(Ptr<const Packet> packet)
{
    uint32_t dumpLen = std::min(packet->GetSize(), (uint32_t)128);
    uint8_t buffer[128];
    packet->CopyData(buffer, dumpLen);

    std::ostringstream oss;
    oss << Simulator::Now().GetSeconds() << "s TX UID=" << packet->GetUid()
        << " Size=" << packet->GetSize() << " bytes" << std::endl;

    std::ostringstream h;
    packet->Print(h);
    if (!h.str().empty())
    {
        oss << "  Headers: " << h.str() << std::endl;
    }

    oss << "   Hex: ";
    for (uint32_t i = 0; i < dumpLen; ++i)
    {
        oss << std::hex << std::setfill('0') << std::setw(2) << (uint32_t)buffer[i] << " ";
        if ((i + 1) % 16 == 0)
        {
            oss << std::endl << "        ";
        }
    }
    oss << std::dec;
    NS_LOG_UNCOND(oss.str());
}

static void
HexDumpRx(Ptr<const Packet> packet, const Address& from)
{
    uint32_t dumpLen = std::min(packet->GetSize(), (uint32_t)128);
    uint8_t buffer[128];
    packet->CopyData(buffer, dumpLen);

    std::ostringstream oss;
    oss << Simulator::Now().GetSeconds() << "s RX UID=" << packet->GetUid()
        << " Size=" << packet->GetSize()
        << " From=";
    if (Mac48Address::IsMatchingType(from))
    {
        oss << Mac48Address::ConvertFrom(from);
    }
    else
    {
        oss << from;
    }
    oss << std::endl;

    oss << "   Hex: ";
    for (uint32_t i = 0; i < dumpLen; ++i)
    {
        oss << std::hex << std::setfill('0') << std::setw(2) << (uint32_t)buffer[i] << " ";
        if ((i + 1) % 16 == 0)
        {
            oss << std::endl << "        ";
        }
    }
    oss << std::dec;
    NS_LOG_UNCOND(oss.str());
}

static void
PcapSniffSink(Ptr<PcapFileWrapper> file, Ptr<const Packet> packet)
{
    file->Write(Simulator::Now(), packet);
}

void
EnablePcapForNodeContainer(NodeContainer nodes,
                           const std::string& prefix,
                           const std::string& outputDir,
                           const std::string& role,
                           bool enableHexDump)
{
    PcapHelper pcapHelper;

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

            std::string devLabel = prefix + "-node" + std::to_string(node->GetId()) + "-dev" +
                                   std::to_string(j);

            SatOrbiterNetDevice* orbDev = dynamic_cast<SatOrbiterNetDevice*>(PeekPointer(dev));
            if (orbDev)
            {
                std::string fullPath = SystemPath::Append(outputDir, devLabel + ".pcap");
                Ptr<PcapFileWrapper> file =
                    pcapHelper.CreateFile(fullPath, std::ios::out, PcapHelper::DLT_RAW);

                orbDev->TraceConnectWithoutContext("Tx", MakeBoundCallback(&PcapTxSink, file));
                orbDev->TraceConnectWithoutContext("RxFeeder", MakeBoundCallback(&PcapRxSink, file));
                orbDev->TraceConnectWithoutContext("RxUser", MakeBoundCallback(&PcapRxSink, file));

                if (enableHexDump)
                {
                    orbDev->TraceConnectWithoutContext("Tx", MakeCallback(&HexDumpTx));
                    orbDev->TraceConnectWithoutContext("RxFeeder", MakeCallback(&HexDumpRx));
                    orbDev->TraceConnectWithoutContext("RxUser", MakeCallback(&HexDumpRx));
                }

                NS_LOG_UNCOND("PCAP (SatOrbiterNetDevice): " << fullPath);
                continue;
            }

            SatNetDevice* satDev = dynamic_cast<SatNetDevice*>(PeekPointer(dev));
            if (satDev)
            {
                std::string fullPath = SystemPath::Append(outputDir, devLabel + ".pcap");
                Ptr<PcapFileWrapper> file =
                    pcapHelper.CreateFile(fullPath, std::ios::out, PcapHelper::DLT_RAW);

                satDev->TraceConnectWithoutContext("Tx", MakeBoundCallback(&PcapTxSink, file));
                satDev->TraceConnectWithoutContext("Rx", MakeBoundCallback(&PcapRxSink, file));
                if (enableHexDump)
                {
                    satDev->TraceConnectWithoutContext("Tx", MakeCallback(&HexDumpTx));
                    satDev->TraceConnectWithoutContext("Rx", MakeCallback(&HexDumpRx));
                }
                NS_LOG_UNCOND("PCAP (SatNetDevice): " << fullPath);
                continue;
            }

            if (typeName == "ns3::CsmaNetDevice")
            {
                std::string fullPath = SystemPath::Append(outputDir, devLabel + ".pcap");
                Ptr<PcapFileWrapper> file =
                    pcapHelper.CreateFile(fullPath, std::ios::out, PcapHelper::DLT_EN10MB);

                dev->TraceConnectWithoutContext("PromiscSniffer", MakeBoundCallback(&PcapSniffSink, file));
                NS_LOG_UNCOND("PCAP (CsmaNetDevice): " << fullPath);
                continue;
            }

            if (typeName == "ns3::PointToPointIslNetDevice")
            {
                std::string fullPath = SystemPath::Append(outputDir, devLabel + ".pcap");
                Ptr<PcapFileWrapper> file =
                    pcapHelper.CreateFile(fullPath, std::ios::out, PcapHelper::DLT_RAW);

                bool connected = false;
                connected = dev->TraceConnectWithoutContext("Tx", MakeBoundCallback(&PcapTxSink, file)) ||
                            connected;
                connected = dev->TraceConnectWithoutContext("Rx", MakeBoundCallback(&PcapRxSink, file)) ||
                            connected;
                connected =
                    dev->TraceConnectWithoutContext("MacTx", MakeBoundCallback(&PcapSniffSink, file)) ||
                    connected;
                connected =
                    dev->TraceConnectWithoutContext("MacRx", MakeBoundCallback(&PcapSniffSink, file)) ||
                    connected;
                connected =
                    dev->TraceConnectWithoutContext("Sniffer", MakeBoundCallback(&PcapSniffSink, file)) ||
                    connected;
                connected = dev->TraceConnectWithoutContext("PromiscSniffer",
                                                            MakeBoundCallback(&PcapSniffSink, file)) ||
                            connected;

                if (connected)
                {
                    NS_LOG_UNCOND("PCAP (PointToPointIslNetDevice): " << fullPath);
                }
                else
                {
                    NS_LOG_UNCOND("SKIP (PointToPointIslNetDevice,no-trace): " << devLabel);
                }
                continue;
            }

            if (typeName == "ns3::SatSimpleNetDevice")
            {
                std::string fullPath = SystemPath::Append(outputDir, devLabel + ".pcap");
                Ptr<PcapFileWrapper> file =
                    pcapHelper.CreateFile(fullPath, std::ios::out, PcapHelper::DLT_RAW);

                bool connected = false;
                connected = dev->TraceConnectWithoutContext("Tx", MakeBoundCallback(&PcapTxSink, file)) ||
                            connected;
                connected = dev->TraceConnectWithoutContext("Rx", MakeBoundCallback(&PcapRxSink, file)) ||
                            connected;
                connected =
                    dev->TraceConnectWithoutContext("MacTx", MakeBoundCallback(&PcapSniffSink, file)) ||
                    connected;
                connected =
                    dev->TraceConnectWithoutContext("MacRx", MakeBoundCallback(&PcapSniffSink, file)) ||
                    connected;
                connected =
                    dev->TraceConnectWithoutContext("Sniffer", MakeBoundCallback(&PcapSniffSink, file)) ||
                    connected;
                connected = dev->TraceConnectWithoutContext("PromiscSniffer",
                                                            MakeBoundCallback(&PcapSniffSink, file)) ||
                            connected;

                if (connected)
                {
                    NS_LOG_UNCOND("PCAP (SatSimpleNetDevice): " << fullPath);
                }
                else
                {
                    NS_LOG_UNCOND("SKIP (SatSimpleNetDevice,no-trace): " << devLabel);
                }
                continue;
            }

            NS_LOG_UNCOND("SKIP (unknown/unsupported): " << devLabel << " Type=" << typeName);
        }
    }
}

} // namespace ns3
