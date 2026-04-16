/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2026 Reshetnev University
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
 * Author: Igor Plastov <chetverovod@gmail.com> Reshetnev University
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
#include "../stats/device-ip-table.h"
#include "../stats/pcap-node-tracing.h"
#include <tuple>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("sat-handover-pcap");

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
    Config::SetDefault("ns3::SatEnvVariables::DataPath",
                       StringValue("contrib/sibgu-hap/data"));
    bool enableHexDump = true;

    std::string simulationName = "sat-handover-pcap";
    Ptr<SimulationHelper> simulationHelper = CreateObject<SimulationHelper>(simulationName);
    std::string fixedOutputDir =
        SystemPath::Append("contrib/sibgu-hap/data/sims", simulationName + "/");
    SystemPath::MakeDirectories(fixedOutputDir);
    simulationHelper->SetOutputPath(fixedOutputDir);
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

    std::string outputDir = Singleton<SatEnvVariables>::Get()->GetOutputPath();
    SystemPath::MakeDirectories(outputDir);

    NS_LOG_UNCOND("Output directory set to: " << outputDir);

    Ptr<SatTopology> topology = Singleton<SatTopology>::Get();

    // ========================================================================
    // Unified device-to-IP mapping table for all roles
    // ========================================================================
    std::vector<DeviceIpRow> ipRows;
    CollectDeviceIpRows(topology->GetGwNodes(), "GW", ipRows);
    CollectDeviceIpRows(topology->GetOrbiterNodes(), "SAT", ipRows);
    CollectDeviceIpRows(topology->GetUtNodes(), "UT", ipRows);
    PrintDeviceIpTable(ipRows);
    SaveDeviceIpTableToFile(ipRows, SystemPath::Append(outputDir, "DevicesTable.txt"));

    // ========================================================================
    // PCAP for all nodes
    // ========================================================================
    EnablePcapForNodeContainer(topology->GetGwNodes(), "sat-handover-gw", outputDir, "GW", enableHexDump);
    EnablePcapForNodeContainer(topology->GetOrbiterNodes(),
                               "sat-handover-orbiter",
                               outputDir,
                               "SAT",
                               enableHexDump);
    EnablePcapForNodeContainer(topology->GetUtNodes(), "sat-handover-ut", outputDir, "UT", enableHexDump);
    
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