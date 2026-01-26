/*
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

// This script based on ns-3.43/examples/wireless/wifi-simple-adhoc.cc
// It configures two nodes on an 802.11b physical layer, with
// 802.11b NICs in adhoc mode, and by default, sends one packet of 1000
// (application) bytes to the other node. 
//
// There are a number of command-line options available to control
// the default behavior.  The list of available command-line options
// can be listed with the following command:
//
// ./ns3 run "wifi-simple-adhoc --help"
//
// For instance, for this configuration, the physical layer will
// stop successfully receiving packets when rss drops below -97 dBm.
//
// Note that all ns-3 attributes (not just the ones exposed in the below
// script) can be changed at command line; see the documentation.
//
// This script can also be helpful to put the Wifi layer into verbose
// logging mode; this command will turn on all wifi logging:
//
// ./ns3 run "wifi-simple-hap --verbose=1"
//
// When you are done, you will notice two pcap trace files in your directory.
// If you have tcpdump installed, you can try this:
//
// tcpdump -r wifi-simple-hap-0-0.pcap -nn -tt
//

#include "ns3/flow-monitor-module.h"
#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/double.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/log.h"
#include "ns3/mobility-helper.h"
#include "ns3/mobility-model.h"
#include "ns3/string.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/yans-wifi-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WifiSimpleAdhoc");

/**
 * Function called when a packet is received.
 *
 * \param socket The receiving socket.
 */
    void
ReceivePacket(Ptr<Socket> socket)
{
    while (socket->Recv())
    {
        // Commented out to avoid cluttering console output.
        //NS_LOG_UNCOND("Received one packet!");
    }
}

/**
 * Generate traffic.
 *
 * \param socket The sending socket.
 * \param pktSize The packet size.
 * \param pktCount The packet count.
 * \param pktInterval The interval between two packets.
 */
    static void
GenerateTraffic(Ptr<Socket> socket, uint32_t pktSize, uint32_t pktCount, Time pktInterval)
{
    if (pktCount > 0)
    {
        socket->Send(Create<Packet>(pktSize));
        Simulator::Schedule(pktInterval,
                &GenerateTraffic,
                socket,
                pktSize,
                pktCount - 1,
                pktInterval);
    }
    else
    {
        socket->Close();
    }
}

    int
main(int argc, char* argv[])
{
    std::string phyMode("DsssRate1Mbps");
    uint32_t packetSize{1000}; // bytes
    uint32_t numPackets{1};
    Time interPacketInterval{"40ms"};
    bool verbose{false};

    // Use this parameter to change the distance between the receiver
    // and the transmitter (network nodes).
    double hight{100.0}; // meters
    double Pdbm{20.}; // Transmitter power.  
    double antGain{20.}; // Transmitter and receiver antenna gain.  

    // Define command line arguments and associated variables.
    CommandLine cmd(__FILE__);
    cmd.AddValue("phyMode", "Wifi Phy mode", phyMode);
    cmd.AddValue("packetSize", "size of application packet sent", packetSize);
    cmd.AddValue("numPackets", "number of packets generated", numPackets);
    cmd.AddValue("interval", "interval between packets", interPacketInterval);
    cmd.AddValue("verbose", "turn on all WifiNetDevice log components", verbose);
    cmd.AddValue("hight", "Distance between nodes (m)", hight);
    cmd.AddValue("txPower", "Power of transmitter, (dBm)", Pdbm);
    cmd.AddValue("antGain", "Antenna gain for transmitter and reciever, (dB)", antGain);
    cmd.Parse(argc, argv);

    // Fix non-unicast data rate to be the same as that of unicast
    Config::SetDefault("ns3::WifiRemoteStationManager::NonUnicastMode", StringValue(phyMode));

    NodeContainer c;
    c.Create(2);

    // The below set of helpers will help us to put together the wifi NICs we want
    WifiHelper wifi;
    if (verbose)
    {
        WifiHelper::EnableLogComponents(); // Turn on all Wifi logging
    }
    wifi.SetStandard(WIFI_STANDARD_80211b);

    YansWifiPhyHelper wifiPhy;

    // Antenna gain
    wifiPhy.Set ("TxGain", DoubleValue (antGain));
    wifiPhy.Set ("RxGain", DoubleValue (antGain));

    // Standard power 20 dBm (100 mW)
    wifiPhy.Set ("TxPowerStart", DoubleValue (Pdbm));
    wifiPhy.Set ("TxPowerEnd", DoubleValue (Pdbm));

    // ns-3 supports RadioTap and Prism tracing extensions for 802.11b
    wifiPhy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

    YansWifiChannelHelper wifiChannel;
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
            
    // 1. Main model of logarithmic distance-dependent path loss
    // Use LogDistance model
    // Exponent: 
    // 2.0 - free space,
    // 3.0 - typical office/urban environment, 
    // 4.0 - heavy attenuation.
    wifiChannel.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
            "Exponent", DoubleValue(2.0),
            "ReferenceDistance", DoubleValue(1.0), // reference distance, m
            "ReferenceLoss", DoubleValue(40.0));  // attenuation at reference distance, dB
    // ReferenceLoss 40dB at reference distance 1m is a realistic value for 2.4GHz

    // 2. ADD RANDOMNESS (Nakagami Fading)
    // This model will add random fluctuations to signal power.
    // m0=1.0 means strong fluctuations (Rayleigh fading), which provides a nice
    // "stepped" loss curve.
    // Without this addition, the transition from the state "No packets lost" to
    // the state "All packets lost" occurs when the distance is increased by 1cm.
    wifiChannel.AddPropagationLoss("ns3::NakagamiPropagationLossModel",
            "m0", DoubleValue(1.0), 
            "m1", DoubleValue(1.0),
            "m2", DoubleValue(1.0));

    wifiPhy.SetChannel(wifiChannel.Create());

    // Add a mac and disable rate control
    WifiMacHelper wifiMac;
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
            "DataMode",
            StringValue(phyMode),
            "ControlMode",
            StringValue(phyMode));

    // Set it to adhoc mode
    wifiMac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, c);


    // Since the transmitter and receiver are stationary, the mobility model
    // is chosen accordingly.
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 0.0, 0.0));
    positionAlloc->Add(Vector(0.0, 0.0, hight));
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(c);

    InternetStackHelper internet;
    internet.Install(c);

    Ipv4AddressHelper ipv4;
    NS_LOG_INFO("Assign IP Addresses.");
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer i = ipv4.Assign(devices);

    TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
    Ptr<Socket> recvSink = Socket::CreateSocket(c.Get(0), tid);
    InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), 80);
    recvSink->Bind(local);
    recvSink->SetRecvCallback(MakeCallback(&ReceivePacket));

    Ptr<Socket> source = Socket::CreateSocket(c.Get(1), tid);

    // Broadcast address
    //InetSocketAddress remote = InetSocketAddress(Ipv4Address("255.255.255.255"), 80);

    // To allow FlowMonitor to work properly
    // we use a specific destination address (Node 0), not broadcast.
    // Otherwise, it will not be able to distinguish data flows.
    InetSocketAddress remote = InetSocketAddress(i.GetAddress(0), 80);

    // For unicast, the SetAllowBroadcast flag is not mandatory, but does not interfere
  //  source->SetAllowBroadcast(true);
    source->Connect(remote);

    // Tracing
    wifiPhy.EnablePcap("wifi-simple-hap", devices);

    // Output what we are doing
    NS_LOG_UNCOND("Testing " << numPackets << " packets sent by HAP on hight " << hight << " m");

    Simulator::ScheduleWithContext(source->GetNode()->GetId(),
            Seconds(1.0),
            &GenerateTraffic,
            source,
            packetSize,
            numPackets,
            interPacketInterval);


    // FlowMonitor
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

    Simulator::Stop (Seconds (44.0));
    Simulator::Run();

    // 7. Collecting statistics
    monitor->CheckForLostPackets ();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

    std::cout << "\n\n--- SIMULATION RESULTS ---\n";
    std::cout << "Conditions\n";
    std::cout << "  Packet size: " << packetSize << " bytes\n";
    std::cout << "  Interval between packets: " << interPacketInterval << "\n";
    std::cout << "  Transmitter, receiver antenna gain: " << antGain << " dB\n";
    std::cout << "  Transmitter power: " << Pdbm << " dBm\n";
    std::cout << "  HAP height: " << hight << " m\n";
    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
        std::cout << "\nFlow " << i->first 
            << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")\n";

        std::cout << "  Sent packets: " << i->second.txPackets << "\n";
        std::cout << "  Received packets:   " << i->second.rxPackets << "\n";

        double lostPackets = i->second.txPackets - i->second.rxPackets;
        double lossRatio = (lostPackets / i->second.txPackets) * 100.0;
        std::cout << "  Lost packets:   " << lostPackets << " (" << lossRatio << "%)\n";

        double throughput = i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds());
        std::cout << "  Throughput: " << throughput / 1024 / 1024 << " Mbps\n";

        if (i->second.rxPackets > 0)
        {
            double delay = i->second.delaySum.GetSeconds() / i->second.rxPackets;
            std::cout << "  Average delay:  " << delay * 1000 << " ms\n";
        }
    }

    monitor->SerializeToXmlFile ("hap-results-clean.xml", true, true);
    std::cout << "-----------------------------\n\n";



    Simulator::Destroy();

    return 0;
}