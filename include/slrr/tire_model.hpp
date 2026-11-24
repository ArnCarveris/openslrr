#pragma once

namespace slrr::physics {

// The values a tyre script writes through WheelRef (stock Tyre.java), in the
// units that script uses. Defaults are the stock Baiern 205/55 sport tyre.
struct TireParameters {
  // setFriction: compound/width input. The peak friction is 1.15 times its
  // ratio to this stock value (#383).
  double friction = 1.76863;
  // setStiffness: the curve's slope at zero slip per unit load (B C D =
  // load x stiffness); the script derives it from profile, rim fit and
  // inflation.
  double stiffness = 17.6839;
  // setSliction: slide/grip ratio, the share of the peak a full slide keeps.
  double sliction = .814;
  // setMaxLoad / setLoadSmooth: reference load and lateral smoothing.
  // Load efficiency decreases above half capacity; zero keeps load linear.
  double maximum_load = 0, load_smoothing = .4;
  // setFrictn_x: longitudinal friction factor.
  double longitudinal_grip = 1;
  // Stored setCPatch tolerance in radians. Not consumed by the 8ac2ba9
  // force law; alignment is supplied separately as a load factor.
  double patch_angle = 0;
};

struct TireForces {
  double longitudinal, lateral;
};

struct TireTuning {
  float lateral_grip = 1.f;
  float longitudinal_grip = 1.f;
  float lateral_response = 1.f;
  float load_sensitivity = 1.f;
};
TireTuning& tire_tuning();

// Peak force along and across the tyre (N): the curve's D with host grip
// tuning. Static holding uses these bounds without requiring motion.
TireForces tire_friction_limits(double load, double alignment,
                                const TireParameters& tire);

// One Magic Formula curve on the combined slip (#383), shaped by the script
// inputs and host tuning. Positive longitudinal pushes along the wheel
// heading; lateral opposes lateral_speed. alignment scales the load before
// the curve is evaluated.
TireForces tire_forces(double load, double alignment, double wheel_speed,
                       double road_speed, double lateral_speed,
                       const TireParameters& tire);

} // namespace slrr::physics
