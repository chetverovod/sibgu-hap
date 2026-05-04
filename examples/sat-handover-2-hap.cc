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
 */

#include "ns3/applications-module.h"
#include "ns3/config-store-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/satellite-enums.h"
#include "ns3/satellite-env-variables.h"
#include "ns3/satellite-module.h"
#include "ns3/system-path.h"
#include "ns3/trace-helper.h"
#include "ns3/traffic-module.h"

#include "../model/orbiter-trajectory-validation.h"
#include "../stats/device-ip-table.h"
#include "../stats/pcap-node-tracing.h"

#include <chrono>
#include <tuple>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("sat-handover-2-hap");

int
main(int argc, char* argv[])
{
    Config::SetDefault("ns3::SatConf::ForwardLinkRegenerationMode",
                       EnumValue(SatEnums::REGENERATION_NETWORK));
    Config::SetDefault("ns3::SatConf::ReturnLinkRegenerationMode",
                       EnumValue(SatEnums::REGENERATION_NETWORK));
    Config::SetDefault("ns3::SatOrbiterFeederPhy::QueueSize", UintegerValue(100000));
    Config::SetDefault("ns3::SatHelper::HandoversEnabled", BooleanValue(true));
    Config::SetDefault("ns3::SatHandoverModule::NumberClosestSats", UintegerValue(3));
    Config::SetDefault("ns3::SatGwMac::DisableSchedulingIfNoDeviceConnected", BooleanValue(true));
    Config::SetDefault("ns3::SatOrbiterMac::DisableSchedulingIfNoDeviceConnected", BooleanValue(true));
    Config::SetDefault("ns3::SatEnvVariables::EnableSimulationOutputOverwrite", BooleanValue(true));
    Config::SetDefault("ns3::SatHelper::PacketTraceEnabled", BooleanValue(true));
    Config::SetDefault("ns3::SatEnvVariables::DataPath", StringValue("contrib/sibgu-hap/data"));
    // LoadScenario() resolves scenarios via SatEnvVariables singleton at runtime.
    // Set the singleton attribute explicitly to avoid falling back to satellite default data path.
    Singleton<SatEnvVariables>::Get()->SetAttribute("DataPath",
                                                    StringValue("contrib/sibgu-hap/data"));

    float simulationDuration = 120.0;
    std::string scenarioName = "constellation-leo-4-satellites-2-hap";
    uint32_t packetSize = 512;
    float interval = 100.0;
    bool enablePcap = false;
    bool enableHexDump = false;

    CommandLine cmd;
    cmd.AddValue("packetSize", "Size of CBR packets in bytes", packetSize);
    cmd.AddValue("interval", "Time interval between CBR packets, in milliseconds", interval);
    cmd.AddValue("scenarioName", "Scenario name", scenarioName);
    cmd.AddValue("simulationDuration", "Simulation duration, in seconds", simulationDuration);
    cmd.AddValue("enablePcap", "Enable PCAP", enablePcap);
    cmd.AddValue("enableHexDump", "Enable Hex-Dump", enableHexDump);

    const std::string simulationName = "sat-handover-2-hap";
    Ptr<SimulationHelper> simulationHelper = CreateObject<SimulationHelper>(simulationName);
    simulationHelper->AddDefaultUiArguments(cmd);
    cmd.Parse(argc, argv);

    const std::string fixedOutputDir =
        SystemPath::Append("contrib/sibgu-hap/data/sims", simulationName + "/");
    SystemPath::MakeDirectories(fixedOutputDir);
    simulationHelper->SetOutputPath(fixedOutputDir);
    simulationHelper->SetSimulationTime(Seconds(simulationDuration));

    const uint32_t utUsersPerUt = 1;
    simulationHelper->SetGwUserCount(1);
    simulationHelper->SetUserCountPerUt(utUsersPerUt);

    std::set<uint32_t> beamSetAll = {1,  2,  3,
                                     4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                                     16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
                                     31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
                                     46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
                                     61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72};
    simulationHelper->SetBeamSet(beamSetAll);

    simulationHelper->LoadScenario(scenarioName);
    simulationHelper->CreateSatScenario(SatHelper::NONE);

    const std::string outputDir = Singleton<SatEnvVariables>::Get()->GetOutputPath();
    SystemPath::MakeDirectories(outputDir);
    Ptr<SatTopology> topology = Singleton<SatTopology>::Get();
    ValidateOrbiterTrajectories(scenarioName, topology);

    std::vector<std::tuple<uint32_t, std::string, uint32_t, std::string, std::string>> ipRows;
    CollectDeviceIpRows(topology->GetGwNodes(), "GW", ipRows);
    CollectDeviceIpRows(topology->GetOrbiterNodes(), "SAT", ipRows);
    CollectDeviceIpRows(topology->GetUtNodes(), "UT", ipRows);
    SaveDeviceIpTableToFile(ipRows, SystemPath::Append(outputDir, "DevicesTable.txt"));

    if (enablePcap)
    {
        EnablePcapForNodeContainer(topology->GetGwNodes(),
                                   "sat-handover-2-hap-gw",
                                   outputDir,
                                   "GW",
                                   enableHexDump);
        EnablePcapForNodeContainer(topology->GetOrbiterNodes(),
                                   "sat-handover-2-hap-orbiter",
                                   outputDir,
                                   "SAT",
                                   enableHexDump);
        EnablePcapForNodeContainer(topology->GetUtNodes(),
                                   "sat-handover-2-hap-ut",
                                   outputDir,
                                   "UT",
                                   enableHexDump);
    }

    NodeContainer utGroupA;
    NodeContainer utGroupB;
    NodeContainer allUtUsers = topology->GetUtUserNodes();
    const uint32_t half = allUtUsers.GetN() / 2;
    for (uint32_t i = 0; i < allUtUsers.GetN(); ++i)
    {
        if (i < half)
        {
            utGroupA.Add(allUtUsers.Get(i));
        }
        else
        {
            utGroupB.Add(allUtUsers.Get(i));
        }
    }

    // Group A traffic via GW user 0.
    simulationHelper->GetTrafficHelper()->AddCbrTraffic(
        SatTrafficHelper::FWD_LINK,
        SatTrafficHelper::UDP,
        MilliSeconds(interval),
        packetSize,
        NodeContainer(topology->GetGwUserNode(0)),
        utGroupA,
        Seconds(1.0),
        Seconds(simulationDuration),
        Seconds(0));
    simulationHelper->GetTrafficHelper()->AddCbrTraffic(
        SatTrafficHelper::RTN_LINK,
        SatTrafficHelper::UDP,
        MilliSeconds(interval),
        packetSize,
        NodeContainer(topology->GetGwUserNode(0)),
        utGroupA,
        Seconds(1.0),
        Seconds(simulationDuration),
        Seconds(0));

    // Group B traffic via GW user 1 when present, otherwise fallback to GW user 0.
    Ptr<Node> gwUserForGroupB =
        (topology->GetGwUserNodes().GetN() > 1) ? topology->GetGwUserNode(1) : topology->GetGwUserNode(0);
    simulationHelper->GetTrafficHelper()->AddCbrTraffic(
        SatTrafficHelper::FWD_LINK,
        SatTrafficHelper::UDP,
        MilliSeconds(interval),
        packetSize,
        NodeContainer(gwUserForGroupB),
        utGroupB,
        Seconds(1.0),
        Seconds(simulationDuration),
        Seconds(0));
    simulationHelper->GetTrafficHelper()->AddCbrTraffic(
        SatTrafficHelper::RTN_LINK,
        SatTrafficHelper::UDP,
        MilliSeconds(interval),
        packetSize,
        NodeContainer(gwUserForGroupB),
        utGroupB,
        Seconds(1.0),
        Seconds(simulationDuration),
        Seconds(0));

    Config::SetDefault("ns3::ConfigStore::Filename",
                       StringValue(SystemPath::Append(outputDir, "sat-handover-2-hap-attributes.xml")));
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

    const auto simulationStart = std::chrono::steady_clock::now();
    simulationHelper->RunSimulation();
    const auto simulationEnd = std::chrono::steady_clock::now();
    const auto simulationElapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(simulationEnd - simulationStart);
    NS_LOG_UNCOND("Simulation wall-clock time: " << (simulationElapsed.count() / 1000.0) << " s");

    return 0;
}
