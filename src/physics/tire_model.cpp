#include "slrr/tire_model.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace slrr::physics {
namespace {
// Combined-slip tyre (#383). One Magic Formula curve on the combined slip
// magnitude, split by the slip direction, as the original law does
// (nicolas-found42/sl-remastered, Steam EXE 00454500): the slip is
// sqrt(sin(alpha)^2 + (cos(alpha) kappa frictn_x)^2), so wheelspin or a
// locked wheel uses up the grip the tyre has sideways.
//
// The shape follows the script's inputs:
// - D: load (with the original's progressive load smoothing above
//   setMaxLoad) times the peak friction. kPeakFriction is the stock tyre's,
//   scaled by setFriction relative to the stock value.
// - C: from setSliction, the slide/grip ratio its script documents: the
//   curve's far value D sin(C pi/2) is sliction times the peak.
// - B: from setStiffness, the slope at zero slip per unit load (B C D = load
//   times stiffness), as in the original.
// - E: -1, the original's setPacejka(8) value.
// For the stock tyre the peak is near 8 degrees and 13 % slip ratio, and a
// full slide keeps 81 % of the peak.
constexpr double kPeakFriction = 1.15;
constexpr double kCurvature = -1;

double magic_formula(double slip, double b, double c, double d, double e) {
  const double bx = b * slip;
  return d * std::sin(c * std::atan(bx - e * (bx - std::atan(bx))));
}

// The original's effective load: above setMaxLoad the load counts less,
// blended by setLoadSmooth.
double effective_load(double load, const TireParameters& tire) {
  if (tire.maximum_load <= 0) return load;
  const auto& tuning = tire_tuning();
  const double smoothing = std::clamp(tire.load_smoothing * tuning.load_sensitivity, 0.0, 1.0);
  constexpr double half_pi = std::numbers::pi / 2;
  const double progressive =
      std::atan(load * half_pi / tire.maximum_load) * tire.maximum_load / half_pi;
  return (1 - smoothing) * std::min(load, tire.maximum_load) + smoothing * progressive;
}

double peak_friction(const TireParameters& tire) {
  constexpr TireParameters stock;
  return kPeakFriction * std::clamp(tire.friction / stock.friction, 0.2, 2.5);
}

// C such that the curve ends at `slide` times its peak (1: no drop).
double shape(const TireParameters& tire) {
  const double slide = std::clamp(tire.sliction, 0.4, 1.0);
  return 2 - 2 / std::numbers::pi * std::asin(slide);
}
} // namespace

TireTuning &tire_tuning() {
  static TireTuning tuning;
  return tuning;
}

TireForces tire_friction_limits(double load, double alignment,
                                const TireParameters& tire) {
  const double aligned_load = std::max(0.0, load * alignment);
  if (aligned_load <= 0) return {};
  const auto& tuning = tire_tuning();
  const double d = effective_load(aligned_load, tire) * peak_friction(tire);
  return {d * std::max(0.0, double(tuning.longitudinal_grip)),
          d * std::max(0.0, double(tuning.lateral_grip))};
}

TireForces tire_forces(double load, double alignment, double wheel_speed,
                       double road_speed, double lateral_speed,
                       const TireParameters &tire) {
  const double aligned_load = std::max(0.0, load * alignment);
  if (aligned_load <= 0.0)
    return {0.0, 0.0};
  const auto& tuning = tire_tuning();

  // Slip ratio and angle against the road speed; below 1.5 m/s that floor
  // keeps them finite, and the low-speed caps below take over.
  const double slip_speed = wheel_speed - road_speed;
  const double reference = std::max(1.5, std::abs(road_speed));
  const double kappa = slip_speed / reference;
  const double alpha = std::atan2(lateral_speed, reference);
  const double sine = std::sin(alpha);
  const double scaled_long =
      std::cos(alpha) * kappa * std::max(0.0, tire.longitudinal_grip);
  const double slip = std::hypot(sine, scaled_long);
  if (slip <= 1e-9) return {0.0, 0.0};

  const double mu = peak_friction(tire);
  const double c = shape(tire);
  const double stiffness =
      std::clamp(tire.stiffness, 4.0, 60.0) * std::max(0.1, double(tuning.lateral_response));
  const double b = stiffness / (c * mu);
  const double d = effective_load(aligned_load, tire) * mu;
  const double force = magic_formula(slip, b, c, d, kCurvature);

  double lateral = -force * sine / slip * tuning.lateral_grip;
  double longitudinal = force * scaled_long / slip * tuning.longitudinal_grip;

  // Low speed: the force grows with the sliding speed, not the slip, so a
  // car at rest is not pushed around by a finite slip of nearly nothing.
  const double lateral_cap = std::abs(lateral_speed) * aligned_load * 0.95;
  lateral = std::copysign(std::min(std::abs(lateral), lateral_cap), lateral);
  const double longitudinal_cap = std::abs(slip_speed) * aligned_load * 0.85;
  longitudinal = std::copysign(std::min(std::abs(longitudinal), longitudinal_cap), longitudinal);
  return {longitudinal, lateral};
}

} // namespace slrr::physics
