#include "handover-guard.h"

#include "ns3/log.h"
#include "ns3/mobility-model.h"
#include "ns3/node.h"
#include "ns3/satellite-antenna-gain-pattern-container.h"
#include "ns3/node-container.h"
#include "ns3/satellite-gw-mac.h"
#include "ns3/satellite-handover-module.h"
#include "ns3/satellite-mac.h"
#include "ns3/satellite-mobility-model.h"
#include "ns3/satellite-net-device.h"
#include "ns3/satellite-topology.h"
#include "ns3/simulator.h"

#include <cmath>

NS_LOG_COMPONENT_DEFINE("HandoverGuard");

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(HandoverGuard);

TypeId
HandoverGuard::GetTypeId()
{
    static TypeId tid = TypeId("ns3::HandoverGuard")
                            .SetParent<Object>()
                            .SetGroupName("SibguHap")
                            .AddConstructor<HandoverGuard>();
    return tid;
}

HandoverGuard::HandoverGuard()
    : m_interval(MilliSeconds(1))
{
}

void
HandoverGuard::DoDispose()
{
    if (m_event.IsPending())
    {
        Simulator::Cancel(m_event);
    }
    m_patterns = nullptr;
    m_groundNodes.clear();
    m_groundRoles.clear();
    Object::DoDispose();
}

void
HandoverGuard::CollectSatMacs(Ptr<Node> node, std::vector<Ptr<SatMac>>& macs)
{
    macs.clear();
    if (node == nullptr)
    {
        return;
    }
    for (uint32_t i = 0; i < node->GetNDevices(); ++i)
    {
        Ptr<SatNetDevice> satDev = DynamicCast<SatNetDevice>(node->GetDevice(i));
        if (satDev == nullptr)
        {
            continue;
        }
        Ptr<SatMac> mac = DynamicCast<SatMac>(satDev->GetMac());
        if (mac != nullptr)
        {
            macs.push_back(mac);
        }
    }
}

void
HandoverGuard::CurrentSatAndBeam(Ptr<SatMac> mac, uint32_t& satId, uint32_t& beamId)
{
    Ptr<SatGwMac> gwMac = DynamicCast<SatGwMac>(mac);
    if (gwMac != nullptr)
    {
        satId = gwMac->GetFeederSatId();
        beamId = gwMac->GetFeederBeamId();
        return;
    }
    satId = mac->GetSatId();
    beamId = mac->GetBeamId();
}

void
HandoverGuard::Install(Ptr<SatTopology> topology, Ptr<SatAntennaGainPatternContainer> patterns)
{
    NS_LOG_FUNCTION(this << topology << patterns);

    if (topology == nullptr || patterns == nullptr)
    {
        NS_LOG_WARN("HandoverGuard::Install skipped: missing topology or antenna patterns");
        return;
    }

    m_patterns = patterns;
    m_nOrbiters = topology->GetOrbiterNodes().GetN();
    m_groundNodes.clear();
    m_groundRoles.clear();
    m_disarmedNodeIds.clear();
    m_armedCount = 0;

    auto addNodes = [this](NodeContainer nodes, const std::string& role) {
        for (uint32_t i = 0; i < nodes.GetN(); ++i)
        {
            Ptr<Node> node = nodes.Get(i);
            if (node->GetObject<SatHandoverModule>() == nullptr)
            {
                continue;
            }
            std::vector<Ptr<SatMac>> macs;
            CollectSatMacs(node, macs);
            if (macs.empty())
            {
                continue;
            }
            m_groundNodes.push_back(node);
            m_groundRoles.push_back(role);
            ++m_armedCount;
        }
    };

    addNodes(topology->GetUtNodes(), "UT");
    addNodes(topology->GetGwNodes(), "GW");

    NodeContainer orbiters = topology->GetOrbiterNodes();
    for (uint32_t i = 0; i < orbiters.GetN(); ++i)
    {
        Ptr<MobilityModel> mobility = orbiters.Get(i)->GetObject<MobilityModel>();
        if (mobility != nullptr)
        {
            mobility->TraceConnectWithoutContext(
                "CourseChange",
                MakeCallback(&HandoverGuard::NotifyCourseChange, this));
        }
    }

    if (m_armedCount == 0 || m_nOrbiters == 0)
    {
        NS_LOG_INFO("HandoverGuard: nothing to watch");
        return;
    }

    NS_LOG_INFO("HandoverGuard watching " << m_armedCount << " ground node(s), " << m_nOrbiters
                                          << " orbiter(s), poll " << m_interval.GetMilliSeconds()
                                          << " ms");
    CheckCoverage();
}

void
HandoverGuard::NotifyCourseChange(Ptr<const MobilityModel> /*model*/)
{
    CheckCoverage();
}

void
HandoverGuard::CheckCoverage()
{
    NS_LOG_FUNCTION(this);

    if (m_patterns == nullptr || m_armedCount == 0)
    {
        return;
    }

    for (size_t i = 0; i < m_groundNodes.size(); ++i)
    {
        CheckNode(m_groundNodes[i], m_groundRoles[i]);
        if (m_armedCount == 0)
        {
            break;
        }
    }

    if (m_armedCount == 0)
    {
        if (m_event.IsPending())
        {
            Simulator::Cancel(m_event);
        }
        return;
    }

    if (!m_event.IsPending())
    {
        m_event = Simulator::Schedule(m_interval, &HandoverGuard::CheckCoverage, this);
    }
}

bool
HandoverGuard::CurrentBeamHasGain(Ptr<Node> node, const GeoCoordinate& coords)
{
    std::vector<Ptr<SatMac>> macs;
    CollectSatMacs(node, macs);
    for (Ptr<SatMac> mac : macs)
    {
        uint32_t satId = 0;
        uint32_t beamId = 0;
        CurrentSatAndBeam(mac, satId, beamId);
        if (beamId == 0)
        {
            continue;
        }
        const double gain = m_patterns->GetBeamGain(satId, beamId, coords);
        if (!std::isnan(gain))
        {
            return true;
        }
    }
    return false;
}

bool
HandoverGuard::HasAnyValidBeam(const GeoCoordinate& coords)
{
    for (uint32_t satId = 0; satId < m_nOrbiters; ++satId)
    {
        if (m_patterns->GetBestBeamId(satId, coords, true) != 0)
        {
            return true;
        }
    }
    return false;
}

void
HandoverGuard::CheckNode(Ptr<Node> node, const std::string& role)
{
    if (node == nullptr || m_disarmedNodeIds.count(node->GetId()) > 0)
    {
        return;
    }

    Ptr<SatMobilityModel> mobility = node->GetObject<SatMobilityModel>();
    if (mobility == nullptr)
    {
        return;
    }

    const GeoCoordinate coords = mobility->GetGeoPosition();
    if (CurrentBeamHasGain(node, coords))
    {
        return;
    }
    if (HasAnyValidBeam(coords))
    {
        return;
    }

    DisarmHandover(node, role);
}

void
HandoverGuard::DisarmHandover(Ptr<Node> node, const std::string& role)
{
    if (node == nullptr || !m_disarmedNodeIds.insert(node->GetId()).second)
    {
        return;
    }

    std::vector<Ptr<SatMac>> macs;
    CollectSatMacs(node, macs);
    for (Ptr<SatMac> mac : macs)
    {
        mac->SetHandoverModule(nullptr);
    }

    if (m_armedCount > 0)
    {
        --m_armedCount;
    }

    NS_LOG_UNCOND("[hapsimulator] No satellite/beam available to hand over service (node "
                  << node->GetId() << ", " << role
                  << "); staying on current link. Simulation continues.");
}

} // namespace ns3
