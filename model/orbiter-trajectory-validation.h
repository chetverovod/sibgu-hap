#ifndef SIBGU_HAP_ORBITER_TRAJECTORY_VALIDATION_H
#define SIBGU_HAP_ORBITER_TRAJECTORY_VALIDATION_H

#include "ns3/core-module.h"
#include "ns3/satellite-module.h"

#include <string>

namespace ns3
{

void ValidateOrbiterTrajectories(const std::string& scenarioName, Ptr<SatTopology> topology);

} // namespace ns3

#endif // SIBGU_HAP_ORBITER_TRAJECTORY_VALIDATION_H
