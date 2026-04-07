/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2014 Magister Solutions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Bastien TAURAN <bastien.tauran@viveris.fr>
 *
 */

#include "ns3/applications-module.h"
#include "ns3/config-store-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/satellite-module.h"
#include "ns3/traffic-module.h"
#include "ns3/trace-helper.h"
#include "ns3/satellite-env-variables.h"
#include "ns3/system-path.h"
#include "ns3/satellite-net-device.h"
#include "ns3/satellite-typedefs.h"
#include "ns3/satellite-enums.h"
#include <iomanip> 
#include <sstream> 

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("sat-handover-pcap");

// ============================================================================
// PCAP коллбэки
// ============================================================================

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

// ============================================================================
// Hex-dump коллбэки
// ============================================================================

static void
HexDumpTx(Ptr<const Packet> packet)
{
    uint32_t dumpLen = std::min(packet->GetSize(), (uint32_t)128);
    uint8_t buffer[128];
    packet->CopyData(buffer, dumpLen);

    std::ostringstream oss;
    oss << Simulator::Now().GetSeconds() << "s TX UID=" << packet->GetUid()
        << " Size=" << packet->GetSize() << " bytes" << std::endl;

    // Заголовки
    std::ostringstream h;
    packet->Print(h);
    if (!h.str().empty())
    {
        oss << "  Headers: " << h.str() << std::endl;
    }

    // Hex
    oss << "   Hex: ";
    for (uint32_t i = 0; i < dumpLen; ++i)
    {
        oss << std::hex << std::setfill('0') << std::setw(2) << (uint32_t)buffer[i] << " ";
        if ((i + 1) % 16 == 0) oss << std::endl << "        ";
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
        oss << from;  // запасной вариант
    }
    oss << std::endl;
            
    oss << "   Hex: ";
    for (uint32_t i = 0; i < dumpLen; ++i)
    {
        oss << std::hex << std::setfill('0') << std::setw(2) << (uint32_t)buffer[i] << " ";
        if ((i + 1) % 16 == 0) oss << std::endl << "        ";
    }
    oss << std::dec;
    NS_LOG_UNCOND(oss.str());
}

// ============================================================================
// PCAP коллбэк для стандартных устройств (Csma, P2P)
// ============================================================================

static void
PcapSniffSink(Ptr<PcapFileWrapper> file, Ptr<const Packet> packet)
{
    file->Write(Simulator::Now(), packet);
}

// ============================================================================
// Функция для печати таблицы IP-адресов (с указанием роли узла: GW, UT, SAT)
// ============================================================================
static void
PrintDeviceIpTable(NodeContainer nodes, std::string role)
{
    std::cout << std::endl;
    std::cout << "====================================================================================" << std::endl;
    std::cout << "| Node ID | Role | Dev ID | Type                          | IP Address             |" << std::endl;
    std::cout << "====================================================================================" << std::endl;

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
            
            // Получаем IP стек
            Ptr<Ipv4> ipStack = node->GetObject<Ipv4>();
            
            if (ipStack)
            {
                int32_t interfaceIndex = ipStack->GetInterfaceForDevice(dev);
                if (interfaceIndex >= 0 && ipStack->GetNAddresses(interfaceIndex) > 0)
                {
                    Ipv4InterfaceAddress ifaceAddr = ipStack->GetAddress(interfaceIndex, 0);
                    
                    // Форматируем IP в строку
                    std::ostringstream ipStream;
                    ipStream << ifaceAddr.GetLocal();
                    ipAddr = ipStream.str();
                }
            }

            // Печатаем строку таблицы с новой колонкой Role
            std::cout << "| " << std::setw(7) << node->GetId() 
                      << " | " << std::setw(4) << role
                      << " | " << std::setw(6) << j
                      << " | " << std::setw(29) << typeName
                      << " | " << std::setw(22) << ipAddr << " |" << std::endl;
        }
    }
    std::cout << "====================================================================================" << std::endl;
    std::cout << std::endl;
}

// ============================================================================
// EnablePcapForNodeContainer
// ============================================================================
void
EnablePcapForNodeContainer(NodeContainer nodes, std::string prefix, std::string outputDir, std::string role)
{
    // 1. Печатаем таблицу, передавая роль (GW, UT или SAT)
    PrintDeviceIpTable(nodes, role);

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

            std::string devLabel = prefix + "-node" + std::to_string(node->GetId())
                                   + "-dev" + std::to_string(j);

            // 2) SatOrbiterNetDevice
            SatOrbiterNetDevice* orbDev = dynamic_cast<SatOrbiterNetDevice*>(PeekPointer(dev));
            if (orbDev)
            {
                std::string fullPath = SystemPath::Append(outputDir, devLabel + ".pcap");
                Ptr<PcapFileWrapper> file = pcapHelper.CreateFile(fullPath, std::ios::out, PcapHelper::DLT_RAW);

                orbDev->TraceConnectWithoutContext("Tx", MakeBoundCallback(&PcapTxSink, file));
                orbDev->TraceConnectWithoutContext("RxFeeder", MakeBoundCallback(&PcapRxSink, file));
                orbDev->TraceConnectWithoutContext("RxUser", MakeBoundCallback(&PcapRxSink, file));

                orbDev->TraceConnectWithoutContext("Tx", MakeCallback(&HexDumpTx));
                orbDev->TraceConnectWithoutContext("RxFeeder", MakeCallback(&HexDumpRx));
                orbDev->TraceConnectWithoutContext("RxUser", MakeCallback(&HexDumpRx));

                NS_LOG_UNCOND("PCAP (SatOrbiterNetDevice): " << fullPath);
                continue;
            }

            // 3) SatNetDevice
            SatNetDevice* satDev = dynamic_cast<SatNetDevice*>(PeekPointer(dev));
            if (satDev)
            {
                std::string fullPath = SystemPath::Append(outputDir, devLabel + ".pcap");
                Ptr<PcapFileWrapper> file = pcapHelper.CreateFile(fullPath, std::ios::out, PcapHelper::DLT_RAW);

                satDev->TraceConnectWithoutContext("Tx", MakeBoundCallback(&PcapTxSink, file));
                satDev->TraceConnectWithoutContext("Rx", MakeBoundCallback(&PcapRxSink, file));

                satDev->TraceConnectWithoutContext("Tx", MakeCallback(&HexDumpTx));
                satDev->TraceConnectWithoutContext("Rx", MakeCallback(&HexDumpRx));

                NS_LOG_UNCOND("PCAP (SatNetDevice): " << fullPath);
                continue;
            }

            // 4) CsmaNetDevice
            if (typeName == "ns3::CsmaNetDevice")
            {
                std::string fullPath = SystemPath::Append(outputDir, devLabel + ".pcap");
                Ptr<PcapFileWrapper> file = pcapHelper.CreateFile(fullPath, std::ios::out, PcapHelper::DLT_EN10MB);

                //dev->TraceConnectWithoutContext("Tx", MakeBoundCallback(&PcapSniffSink, file));
                //dev->TraceConnectWithoutContext("MacTx", MakeBoundCallback(&PcapSniffSink, file));
                dev->TraceConnectWithoutContext("PromiscSniffer", MakeBoundCallback(&PcapSniffSink, file));
                NS_LOG_UNCOND("PCAP (CsmaNetDevice): " << fullPath);
                continue;
            }

            // 5) PointToPointIslNetDevice
            if (typeName == "ns3::PointToPointIslNetDevice")
            {
                NS_LOG_UNCOND("SKIP (PointToPointIslNetDevice): " << devLabel);
                continue;
            }

            NS_LOG_UNCOND("SKIP (unknown/unsupported): " << devLabel << " Type=" << typeName);
        }
    }
}

// ============================================================================
// main
// ============================================================================
int
main(int argc, char* argv[])
{
    Config::SetDefault("ns3::SatConf::ForwardLinkRegenerationMode",
                       EnumValue(SatEnums::REGENERATION_NETWORK));
    Config::SetDefault("ns3::SatConf::ReturnLinkRegenerationMode",
                       EnumValue(SatEnums::REGENERATION_NETWORK));
    Config::SetDefault("ns3::SatOrbiterFeederPhy::QueueSize", UintegerValue(100000));
    Config::SetDefault("ns3::SatHelper::HandoversEnabled", BooleanValue(true));
    Config::SetDefault("ns3::SatHandoverModule::NumberClosestSats", UintegerValue(2));
    Config::SetDefault("ns3::SatGwMac::DisableSchedulingIfNoDeviceConnected", BooleanValue(true));
    Config::SetDefault("ns3::SatOrbiterMac::DisableSchedulingIfNoDeviceConnected", BooleanValue(true));
    Config::SetDefault("ns3::SatEnvVariables::EnableSimulationOutputOverwrite", BooleanValue(true));
    Config::SetDefault("ns3::SatHelper::PacketTraceEnabled", BooleanValue(true));

    std::string simulationName = "sat-handover-pcap";
    Ptr<SimulationHelper> simulationHelper = CreateObject<SimulationHelper>(simulationName);
    simulationHelper->SetSimulationTime(Seconds(100));
    uint32_t utUsers = 1;
    simulationHelper->SetGwUserCount(utUsers);
    simulationHelper->SetUserCountPerUt(utUsers);

    std::set<uint32_t> beamSetAll = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                                     16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
                                     31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
                                     46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
                                     61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72};
    simulationHelper->SetBeamSet(beamSetAll);
    
    simulationHelper->LoadScenario("constellation-leo-2-satellites");
    simulationHelper->CreateSatScenario(SatHelper::NONE);

    std::string dataPath = Singleton<SatEnvVariables>::Get()->GetDataPath();
    std::string outputDir = SystemPath::Append(dataPath, "sims/" + simulationName);
    SystemPath::MakeDirectories(outputDir);

    NS_LOG_UNCOND("Output directory set to: " << outputDir);

    Ptr<SatTopology> topology = Singleton<SatTopology>::Get();

    // ========================================================================
    // PCAP для всех нод (с указанием роли: GW, SAT, UT)
    // ========================================================================
    EnablePcapForNodeContainer(topology->GetGwNodes(), "sat-handover-gw", outputDir, "GW");
    EnablePcapForNodeContainer(topology->GetOrbiterNodes(), "sat-handover-orbiter", outputDir, "SAT");
    EnablePcapForNodeContainer(topology->GetUtNodes(), "sat-handover-ut", outputDir, "UT");
    
    // ========================================================================
    // Traffic
    // ========================================================================
    simulationHelper->GetTrafficHelper()->AddCbrTraffic(
        SatTrafficHelper::FWD_LINK, SatTrafficHelper::UDP, MilliSeconds(100), 512,
        NodeContainer(Singleton<SatTopology>::Get()->GetGwUserNode(0)),
        Singleton<SatTopology>::Get()->GetUtUserNodes(),
        Seconds(1.0), Seconds(100.0), Seconds(0));

    simulationHelper->GetTrafficHelper()->AddCbrTraffic(
        SatTrafficHelper::RTN_LINK, SatTrafficHelper::UDP, MilliSeconds(100), 512,
        NodeContainer(Singleton<SatTopology>::Get()->GetGwUserNode(0)),
        Singleton<SatTopology>::Get()->GetUtUserNodes(),
        Seconds(1.0), Seconds(100.0), Seconds(0));

    Config::SetDefault("ns3::ConfigStore::Filename", StringValue(SystemPath::Append(outputDir, "my_example-handover-attributes.xml")));
    Config::SetDefault("ns3::ConfigStore::FileFormat", StringValue("Xml"));
    Config::SetDefault("ns3::ConfigStore::Mode", StringValue("Save"));
    ConfigStore outputConfig;
    outputConfig.ConfigureDefaults();

    Ptr<SatStatsHelperContainer> s = simulationHelper->GetStatisticsContainer();
    s->AddPerSatFwdAppThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerSatFwdUserDevThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerSatRtnAppThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerSatRtnUserDevThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerBeamFwdAppThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerBeamFwdUserDevThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerBeamBeamServiceTime(SatStatsHelper::OUTPUT_SCALAR_FILE);

    simulationHelper->EnableProgressLogs();
    simulationHelper->RunSimulation();

    return 0;
}