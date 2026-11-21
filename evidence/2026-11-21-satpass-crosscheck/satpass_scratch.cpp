// scratch harness: print pass windows from the repo's radio::satellite layer
// build: cl /std:c++17 /EHsc /I ... satpass_scratch.cpp radio_satellite.lib radio_sgp4.lib
#include <libsgp4/DateTime.h>
#include <libsgp4/Tle.h>

#include "satellite/sat_track.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
  if (argc < 6) {
    std::fprintf(stderr, "usage: %s <tlefile> <lat> <lon> <alt_km> <hours>\n", argv[0]);
    return 2;
  }
  std::ifstream f(argv[1]);
  std::string l1, l2;
  std::getline(f, l1);  // name
  std::getline(f, l1);
  std::getline(f, l2);
  while (!l1.empty() && (l1.back() == '\r' || l1.back() == ' ')) l1.pop_back();
  while (!l2.empty() && (l2.back() == '\r' || l2.back() == ' ')) l2.pop_back();

  libsgp4::Tle tle(l1, l2);
  radio::satellite::Station st{std::atof(argv[2]), std::atof(argv[3]), std::atof(argv[4])};
  const double hours = std::atof(argv[5]);
  const double from = radio::satellite::tsince_min(tle, libsgp4::DateTime::Now());
  const auto passes = radio::satellite::find_passes(tle, st, from, from + hours * 60.0, 5.0);

  const libsgp4::DateTime epoch = tle.Epoch();
  std::printf("norad=%u epoch=%s window_from_min=%.1f passes=%zu\n",
              tle.NoradNumber(), epoch.ToString().c_str(), from, passes.size());
  for (const auto& p : passes) {
    std::printf("PASS aos=%s tca=%s los=%s max_el=%.3f min_range=%.3f max_abs_rr=%.5f\n",
                epoch.AddMinutes(p.aos_min).ToString().c_str(),
                epoch.AddMinutes(p.tca_min).ToString().c_str(),
                epoch.AddMinutes(p.los_min).ToString().c_str(), p.max_el_deg,
                p.min_range_km, p.max_abs_range_rate_kms);
  }
  return 0;
}
