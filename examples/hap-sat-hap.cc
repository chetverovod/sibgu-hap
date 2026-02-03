/*
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

// Scenario: Satellite Module - Two Gateways communicating via GEO Satellite.
// - Group 1: Users behind Gateway 1.
// - Group 2: Users behind Gateway 2.
// - Backbone: GEO Satellite.
// - Traffic: UDP Sockets from Group 1 to Group 2.
// - Stats: FlowMonitor.

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/satellite-module.h"
#include "ns3/traffic-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/config-store-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("SatelliteModuleHapTest");

// --- Traffic Generation Callbacks (Sockets) ---

void ReceivePacket(Ptr<Socket> socket)
{
    while (socket->Recv())
    {
        // Packet received
    }
}

static void GenerateTraffic(Ptr<Socket> socket, uint32_t pktSize, uint32_t pktCount, Time pktInterval)
{
    if (pktCount > 0)
    {
        socket->Send(Create<Packet>(pktSize));
        Simulator::Schedule(pktInterval, &GenerateTraffic, socket, pktSize, pktCount - 1, pktInterval);
    }
    else
    {
        socket->Close();
    }
}

int
main(int argc, char* argv[])
{
    // --- Parameters ---
    uint32_t packetSize = 128; // bytes (typical for satellite)
    uint32_t numPackets = 1000;
    std::string intervalStr("0.01s"); // 10ms
    double simLength(20.0);
    
    // Parse command line
    CommandLine cmd;
    cmd.AddValue("packetSize", "Size of packet (bytes)", packetSize);
    cmd.AddValue("numPackets", "Number of packets", numPackets);
    cmd.AddValue("interval", "Interval between packets", intervalStr);
    cmd.AddValue("simLength", "Simulation length (seconds)", simLength);
    cmd.Parse(argc, argv);

    Time interPacketInterval = Time(intervalStr);

    // --- 1. Simulation Helper Setup ---
    
    // Enable simulation output overwrite
    Config::SetDefault("ns3::SatEnvVariables::EnableSimulationOutputOverwrite", BooleanValue(true));

    // Disable default packet traces to keep console clean
    Config::SetDefault("ns3::SatHelper::PacketTraceEnabled", BooleanValue(false));

    Ptr<SimulationHelper> simulationHelper = CreateObject<SimulationHelper>("sat-hap-socket-test");

    // --- 2. Scenario Configuration ---
    
    simulationHelper->SetSimulationTime(simLength);

    // Configure error model (optional, can be constant or disabled)
    // Here we disable errors for clean test of topology
    SatPhyRxCarrierConf::ErrorModel em(SatPhyRxCarrierConf::EM_NONE);
    Config::SetDefault("ns3::SatUtHelper::FwdLinkErrorModel", EnumValue(em));
    Config::SetDefault("ns3::SatGwHelper::RtnLinkErrorModel", EnumValue(em));

    // Load GEO scenario and Create Topology
    simulationHelper->LoadScenario("geo-33E");
    simulationHelper->CreateSatScenario(SatHelper::FULL);

    // --- 3. Node Selection (Mapping to HAPs) ---
    
    // In the Satellite Module, Gateways (GW) act as the entry/exit points for terrestrial networks.
    // We treat GW 1 as "HAP 1" and GW 2 as "HAP 2".
    // We will send traffic from a User behind GW 1 to a User behind GW 2.

    Ptr<SatTopology> topology = Singleton<SatTopology>::Get();

    // Check if we have at least 2 Gateways in the scenario
    if (topology->GetGwNodes().GetN() < 2)
    {
        NS_LOG_UNCOND("Error: The selected scenario must have at least 2 Gateways to simulate two HAP groups.");
        return 1;
    }

    // Get User Nodes (End users attached to GWs)
    // Index 0 corresponds to GW 0 (Group 1), Index 1 corresponds to GW 1 (Group 2)
    Ptr<Node> sourceNode = topology->GetGwUserNode(0); // Source: User at Group 1
    Ptr<Node> sinkNode = topology->GetGwUserNode(1);   // Sink: User at Group 2

    NS_LOG_UNCOND("--- Satellite Module Test (Socket Version) ---");
    NS_LOG_UNCOND("Source: User at GW 0 (IP: " << sourceNode->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal() << ")");
    NS_LOG_UNCOND("Sink:   User at GW 1 (IP: " << sinkNode->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal() << ")");

    // --- 4. Application Setup (Sockets) ---
    
    uint16_t port = 9;

    // Install Sink (Receiver)
    TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
    Ptr<Socket> recvSink = Socket::CreateSocket(sinkNode, tid);
    InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), port);
    recvSink->Bind(local);
    recvSink->SetRecvCallback(MakeCallback(&ReceivePacket));

    // Install Source (Sender)
    Ptr<Socket> source = Socket::CreateSocket(sourceNode, tid);
    Ipv4Address sinkAddress = sinkNode->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
    InetSocketAddress remote = InetSocketAddress(sinkAddress, port);
    source->Connect(remote);

    // --- 5. Flow Monitor Setup ---
    
    FlowMonitorHelper flowmon;
    // Install on all nodes to see the whole path (GWs, Satellite, UTs)
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    // --- 6. Start Simulation ---

    // Schedule Traffic Start
    Simulator::ScheduleWithContext(source->GetNode()->GetId(),
            Seconds(1.0),
            &GenerateTraffic,
            source,
            packetSize,
            numPackets,
            interPacketInterval);

    // Stop Simulation
    Simulator::Stop(Seconds(simLength));
    Simulator::Run();

    // --- 7. Statistics Output ---
    
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    std::cout << "\n\n--- SIMULATION RESULTS (Satellite Module) ---\n";
    std::cout << "Topology: GW User 0 <-> GEO Satellite <-> GW User 1\n";
    std::cout << "Conditions:\n";
    std::cout << "  Packet size: " << packetSize << " bytes\n";
    std::cout << "  Number of packets: " << numPackets << "\n";
    std::cout << "  Interval: " << interPacketInterval.GetMilliSeconds() << " ms\n";
    std::cout << "  Scenario: geo-33E (Full)\n";

    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin(); i != stats.end(); ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);
        
        // Filter only our flow if needed, or print all. 
        // Here we print all relevant flows.
        
        std::cout << "\nFlow " << i->first 
            << " (" << t.sourceAddress << ":" << t.sourcePort << " -> " 
            << t.destinationAddress << ":" << t.destinationPort << ")\n";

        std::cout << "  Tx Packets: " << i->second.txPackets << "\n";
        std::cout << "  Rx Packets: " << i->second.rxPackets << "\n";

        if (i->second.txPackets > 0)
        {
            double lostPackets = i->second.txPackets - i->second.rxPackets;
            double lossRatio = (lostPackets / i->second.txPackets) * 100.0;
            std::cout << "  Lost Packets: " << lostPackets << " (" << lossRatio << "%)\n";
        }

        if (i->second.rxPackets > 0)
        {
             double throughput = i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds());
             std::cout << "  Throughput: " << throughput / 1024 << " Kbps\n";
             double delay = i->second.delaySum.GetSeconds() / i->second.rxPackets;
             std::cout << "  Avg Delay:  " << delay * 1000 << " ms\n";
        }
    }

    monitor->SerializeToXmlFile("sat-module-stats.xml", true, true);
    std::cout << "---------------------------------------------\n\n";

    Simulator::Destroy();

    return 0;
}