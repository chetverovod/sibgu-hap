#include "dynamics-schedule.h"

#include "ns3/log.h"
#include "ns3/satellite-antenna-gain-pattern-container.h"
#include "ns3/satellite-antenna-gain-pattern.h"
#include "ns3/simulator.h"

#include <sstream>
#include <utility>

NS_LOG_COMPONENT_DEFINE("DynamicsSchedule");

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(DynamicsSchedule);

TypeId
DynamicsSchedule::GetTypeId()
{
    static TypeId tid = TypeId("ns3::DynamicsSchedule")
                            .SetParent<Object>()
                            .SetGroupName("SibguHap")
                            .AddConstructor<DynamicsSchedule>();
    return tid;
}

DynamicsSchedule::DynamicsSchedule() = default;

void
DynamicsSchedule::DoDispose()
{
    if (m_log.is_open())
    {
        m_log.close();
    }
    m_patterns = nullptr;
    Object::DoDispose();
}

const std::vector<DynamicsEvent>&
DynamicsSchedule::Events() const
{
    return m_events;
}

const DynamicsBeamRef*
DynamicsSchedule::FindBeam(uint32_t beamId) const
{
    auto it = m_beams.find(beamId);
    if (it == m_beams.end())
    {
        return nullptr;
    }
    return &it->second;
}

uint32_t
DynamicsSchedule::AppliedCount() const
{
    return m_applied;
}

bool
DynamicsSchedule::ParseDouble(const std::string& token, double& value)
{
    if (token == "-" || token.empty())
    {
        return false;
    }
    std::istringstream in(token);
    in >> value;
    return !in.fail();
}

bool
DynamicsSchedule::LoadFile(const std::string& path)
{
    NS_LOG_FUNCTION(this << path);
    m_events.clear();
    m_beams.clear();
    std::ifstream in(path);
    if (!in.is_open())
    {
        return false;
    }
    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        std::istringstream row(line);
        std::string kind;
        row >> kind;
        if (kind == "beam")
        {
            DynamicsBeamRef ref;
            uint32_t beamId = 0;
            std::string latToken;
            std::string lonToken;
            row >> beamId >> latToken >> lonToken;
            std::string name;
            std::getline(row, name);
            if (!name.empty() && name[0] == ' ')
            {
                name.erase(0, 1);
            }
            ParseDouble(latToken, ref.latDeg);
            ParseDouble(lonToken, ref.lonDeg);
            ref.name = name;
            if (beamId != 0)
            {
                m_beams[beamId] = std::move(ref);
            }
        }
        else if (kind == "event")
        {
            DynamicsEvent event;
            std::string timeToken;
            std::string latToken;
            std::string lonToken;
            std::string freqToken;
            std::string bwToken;
            row >> timeToken >> event.beamId >> latToken >> lonToken >> freqToken >> bwToken;
            if (!ParseDouble(timeToken, event.timeS) || event.beamId == 0)
            {
                continue;
            }
            event.hasPointing = ParseDouble(latToken, event.latDeg) && ParseDouble(lonToken, event.lonDeg);
            double freq = 0.0;
            event.hasCarrier = ParseDouble(freqToken, freq);
            event.freqHz = freq;
            ParseDouble(bwToken, event.bandwidthHz);
            m_events.push_back(event);
        }
    }
    return !m_events.empty();
}

void
DynamicsSchedule::Install(Ptr<SatAntennaGainPatternContainer> patterns, const std::string& logPath)
{
    NS_LOG_FUNCTION(this << patterns << logPath);
    m_patterns = patterns;
    m_applied = 0;
    if (!logPath.empty())
    {
        m_log.open(logPath, std::ios::out | std::ios::trunc);
        if (m_log.is_open())
        {
            m_log << "# time_s beam_id lat_deg lon_deg shift_lat shift_lon freq_hz bandwidth_hz\n";
        }
    }
    NS_LOG_UNCOND("[dynamics] schedule events: " << m_events.size());
    for (const auto& event : m_events)
    {
        Simulator::Schedule(Seconds(event.timeS), &DynamicsSchedule::Apply, this, event);
    }
}

void
DynamicsSchedule::Apply(DynamicsEvent event)
{
    double shiftLat = 0.0;
    double shiftLon = 0.0;
    const DynamicsBeamRef* ref = FindBeam(event.beamId);
    if (event.hasPointing && ref != nullptr && m_patterns != nullptr &&
        m_patterns->HasAntennaGainPattern(event.beamId))
    {
        shiftLat = ref->latDeg - event.latDeg;
        shiftLon = ref->lonDeg - event.lonDeg;
        m_patterns->GetAntennaGainPattern(event.beamId)->SetPointingShift(shiftLat, shiftLon);
    }
    if (m_log.is_open())
    {
        m_log << event.timeS << " " << event.beamId << " ";
        if (event.hasPointing)
        {
            m_log << event.latDeg << " " << event.lonDeg << " ";
        }
        else
        {
            m_log << "- - ";
        }
        m_log << shiftLat << " " << shiftLon << " ";
        if (event.hasCarrier)
        {
            m_log << event.freqHz << " " << event.bandwidthHz;
        }
        else
        {
            m_log << "- -";
        }
        m_log << "\n";
    }
    NS_LOG_DEBUG("beam " << event.beamId << " t=" << event.timeS << " shift " << shiftLat << ","
                         << shiftLon);
    ++m_applied;
}

} // namespace ns3
