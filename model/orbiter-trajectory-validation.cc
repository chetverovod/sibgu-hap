#include "orbiter-trajectory-validation.h"

#include "ns3/satellite-env-variables.h"
#include "ns3/system-path.h"
#include "ns3/satellite-sgp4-mobility-model.h"
#include "ns3/satellite-traced-mobility-model.h"

#include <fstream>
#include <set>
#include <sstream>

namespace ns3
{

static std::set<uint32_t>
LoadTraceSatIds(const std::string& scenarioName)
{
    std::set<uint32_t> traceSatIds;
    std::string basePath = Singleton<SatEnvVariables>::Get()->LocateDataDirectory();
    std::string traceMap =
        SystemPath::Append(basePath, "scenarios/" + scenarioName + "/positions/sat_traces.txt");

    if (!Singleton<SatEnvVariables>::Get()->IsValidFile(traceMap))
    {
        return traceSatIds;
    }

    std::ifstream input(traceMap);
    NS_ABORT_MSG_UNLESS(input.is_open(), "Cannot open sat traces map file: " << traceMap);

    std::string line;
    while (std::getline(input, line))
    {
        if (line.empty() || line[0] == '%')
        {
            continue;
        }

        std::istringstream iss(line);
        uint32_t satId = 0;
        std::string tracePath;
        if (!(iss >> satId >> tracePath))
        {
            continue;
        }

        NS_ABORT_MSG_UNLESS(
            Singleton<SatEnvVariables>::Get()->IsValidFile(tracePath),
            "Trajectory loading failed: satId=" << satId << " has invalid trace path '"
                                                << tracePath
                                                << "' in sat_traces.txt");
        traceSatIds.insert(satId);
    }

    return traceSatIds;
}

void
ValidateOrbiterTrajectories(const std::string& scenarioName, Ptr<SatTopology> topology)
{
    std::set<uint32_t> traceSatIds = LoadTraceSatIds(scenarioName);
    NodeContainer orbiters = topology->GetOrbiterNodes();

    NS_ABORT_MSG_UNLESS(orbiters.GetN() > 0, "Scenario contains no orbiter nodes");

    for (uint32_t satId = 0; satId < orbiters.GetN(); ++satId)
    {
        Ptr<Node> satNode = orbiters.Get(satId);
        Ptr<SatTracedMobilityModel> tracedModel = satNode->GetObject<SatTracedMobilityModel>();
        Ptr<SatSGP4MobilityModel> tleModel = satNode->GetObject<SatSGP4MobilityModel>();
        bool shouldBeTraced = traceSatIds.count(satId) > 0;

        if (shouldBeTraced)
        {
            NS_ABORT_MSG_UNLESS(tracedModel != nullptr,
                                "Trajectory loading failed: satId=" << satId
                                << " must use SatTracedMobilityModel from sat_traces.txt");
        }
        else
        {
            NS_ABORT_MSG_UNLESS(tleModel != nullptr,
                                "Trajectory loading failed: satId=" << satId
                                << " must use SatSGP4MobilityModel from tles.txt");
        }
    }
}

} // namespace ns3
