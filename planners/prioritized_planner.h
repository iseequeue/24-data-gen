#pragma once

#include "spdlog/spdlog.h"

#include <Core/array.h>
#include <KOMO/komo.h>
#include <Kin/kin.h>
#include <Optim/optimization.h>

#include <PlanningSubroutines/Animation.h>
#include <Manip/timedPath.h>
#include <PlanningSubroutines/ConfigurationProblem.h>
#include <Manip/rrt-time.h>
#include <Geo/fclInterface.h>

#include "plan.h"
#include "postprocessing.h"

#include "common/util.h"
#include "common/env_util.h"
#include "common/config.h"

struct PlannerOptions{
  bool early_stopping{false};
};

struct PathPlannerOptions {
  // optimizatoin based planning
  bool attempt_komo{false};

  // sampling based planning
  bool shortcut{true};
  bool smooth{true};
};

rai::Array<rai::KinematicSwitch>
switches_from_skeleton(const Skeleton &S, const rai::Configuration &C) {
  intA switches = getSwitchesFromSkeleton(S, C);

  rai::Array<rai::KinematicSwitch> ks;
  for (uint i = 0; i < switches.d0; ++i) {
    int k = switches(i, 1);
    const double time = S(k).phase0;

    char *from = S(k).frames(0);
    char *to = S(k).frames.last();
    rai::JointType type = rai::JT_rigid;

    rai::KinematicSwitch tmp(rai::SW_joint, type, from, to, C, rai::SWInit_copy,
                             0, NoTransformation, NoTransformation);
    tmp.timeOfApplication = time;

    ks.append(tmp);
  }

  return ks;
}

void set_to_mode(const Skeleton &S, rai::Configuration &C,
                 const std::vector<arr> keyframes, const uint t) {
  rai::Array<rai::KinematicSwitch> ks(switches_from_skeleton(S, C));

  // iterate over all switches, and check if it is an active switch, if yes,
  // apply it
  for (uint j = 0; j < ks.d0; ++j) {
    if (ks(j).timeOfApplication + 1 <= (int)t) {
      // if(verbose_)std::cout << "Time of application " <<
      // ks(j)->timeOfApplication << std::endl; if(verbose_)std::cout <<
      // "Applying switch at time " << t << std::endl;
      ks(j).apply(C.frames);
    }
  }
}

FrameL get_robot_frames(rai::Configuration &C, const Robot &robot) {
  FrameL frames;

  const rai::String base = STRING(robot << "base");
  FrameL roots = C.getFrames(C.getFrameIDs({base}));
  for (auto *f : roots) {
    f->getSubtree(frames);
  }
  frames.append(roots);

  // remove any objs that might otherwise beome part of the frame-list
  FrameL objs_in_tree;
  for (auto f: frames){
    if (f->name.contains("obj")){
      objs_in_tree.append(f);
    }
  }

  for (const auto f: objs_in_tree){
    frames.removeValue(f);
  }

  return frames;
}

FrameL get_robot_joints(rai::Configuration &C, const Robot &robot) {
  FrameL frames = get_robot_frames(C, robot);
  FrameL joints;
  for (const auto f: frames){
    if (f->joint){
      joints.append(f);
    }
  }

  return joints;
}

arr plan_with_komo_given_horizon(const rai::Animation &A, rai::Configuration &C,
                                 const arr &q0, const arr &q1, const arr &ts,
                                 const Robot r, double &ineq,
                                 double &eq) {
  // TODO: smarter scaling computation
  const double scaling = 3;
  const uint num_timesteps = ts.N / scaling;

  arr scaled_ts(num_timesteps);
  for (uint i=0; i<num_timesteps; ++i){
    scaled_ts(i) = ts(0) + (ts(-1) - ts(0)) / (num_timesteps - 1) * i;
  }

  OptOptions options;
  options.stopIters = 50;
  options.damping = 1e-3;
  options.stopLineSteps = 5;
  // options.stopTolerance = 0.1;

  spdlog::info("setting up komo");
  KOMO komo;

  komo.setModel(C, true);
  komo.world.fcl()->stopEarly = global_params.use_early_coll_check_stopping;

  komo.setTiming(1., num_timesteps, 5, 2);
  komo.verbose = 0;
  komo.solver = rai::KS_sparse;

  komo.add_collision(true, .1, 1e2);
  komo.add_jointLimits(true, 0., 1e1);

  komo.add_qControlObjective({}, 2, 1e0);
  komo.add_qControlObjective({}, 1, 1e1);

  komo.setConfiguration(-2, q0);
  komo.setConfiguration(-1, q0);
  komo.setConfiguration(0, q0);

  // make pen tip go a way from the table
  const double offset = 0.1;
  komo.addObjective({0.3, 0.7}, FS_distance,
                    {"table", STRING(r.prefix << r.ee_frame_name)}, OT_ineq, {1e0},
                    {-offset});
  // komo.addObjective({0.1, 0.8}, FS_distance,
  //                  {"table", STRING(prefix << "pen_tip")}, OT_sos, {1e1});

  // position
  komo.addObjective({0}, FS_qItself, {}, OT_eq, {1e1}, q0);
  komo.addObjective({1}, FS_qItself, {}, OT_eq, {1e2}, q1);

  // speed
  komo.addObjective({0.0, 0.05}, FS_qItself, {}, OT_eq, {1e1}, {},
                   2); // slow at beginning
  komo.addObjective({0.95, 1.0}, FS_qItself, {}, OT_eq, {1e1}, {},
                    1); // slow at end

  // acceleration
  // komo.addObjective({0.0, 0.05}, FS_qItself, {}, OT_eq, {1e1}, {},
  //                  2); // slow at beginning
  komo.addObjective({0.95, 1.0}, FS_qItself, {}, OT_eq, {1e1}, {},
                    2); // slow at end

  if (false) {
    const arr v_constr = q0 * 0. + r.vmax;
    komo.addObjective({0.0, 1.}, FS_qItself, {}, OT_ineq, {1e0}, {v_constr},
                      1); // slow at beginning
    komo.addObjective({0.0, 1.}, FS_qItself, {}, OT_ineq, {-1e0}, {-v_constr},
                      1); // slow at beginning
  }

  setKomoToAnimation(komo, C, A, scaled_ts);

  spdlog::info("Running komo planner");

  komo.run_prepare(0.01);
  komo.run(options);

  spdlog::info("Finished komo planner");

  /*if (komo.getReport(false).get<double>("ineq") > 1. ||
  komo.getReport(false).get<double>("eq") > 1.){ std::cout << "infeasible komo
  sol, ineq: " << komo.getReport(false).get<double>("ineq")
      << " eq. " << komo.getReport(false).get<double>("eq") << std::endl;
    return {};
  }*/

  ineq = komo.getReport(false).get<double>("ineq");
  eq = komo.getReport(false).get<double>("eq");

  // if (eq > 2 || ineq > 2){
  // komo.getReport(true);
  // komo.pathConfig.watch(true);
  //}

  // komo.pathConfig.watch(true);

  // check if the path is actually feasible
  arr path(scaled_ts.N, q0.N);
  for (uint j = 0; j < scaled_ts.N; ++j) {
    path[j] = komo.getPath_q(j);
  }

  if (length(path[0] - q0) > 1e-3) {
    spdlog::debug("Start pose difference to desired {:03.2f}",
                  length(path[0] - q0));

    return {};
  }

  if (length(path[-1] - q1) > 1e-3) {
    spdlog::debug("Goal pose difference to desired {:03.2f}",
                  length(path[-1] - q1));
    
    return {};
  }

  path[0] = q0;
  path[-1] = q1;

  // rescale path
  TimedPath tp(path, scaled_ts);
  const arr rescaled_path = tp.resample(ts, C);

  //std::cout << rescaled_path.d0 << " " << path.d0 << " " << ts.d0 << " " << scaled_ts.d0 << std::endl;

  return rescaled_path;
}

double get_max_speed(const arr &path) {
  double max_speed = 0;
  for (uint i = 0; i < path.d0 - 1; ++i) {
    max_speed = std::max({max_speed, absMax(path[i] - path[i + 1])});
    // max_speed = std::max({max_speed, length(path[i] - path[i + 1])});
  }

  return max_speed;
}

double get_earliest_feasible_time(TimedConfigurationProblem &TP, const arr &q,
                                  const uint t_max, const uint t_min) {
  // idea: start at the maximum time (where we know that ut is feasible),
  // and decrease the time, and check if it is feasible at this time
  uint t_earliest_feas = t_max;
  while (t_earliest_feas > t_min) {
    const auto res = TP.query(q, t_earliest_feas);
    if (!res->isFeasible) {
      spdlog::info("Not feasible at time {}", t_earliest_feas);
      t_earliest_feas += 2;
      break;
    }
    --t_earliest_feas;
  }

  // if (t_max == t_earliest_feas + 2) {
  //   spdlog::error("Configuration never feasible.");
  //   spdlog::error("tmax {}, tmin {}, anim duration {}", t_max, t_min, TP.A.getT());
    
  //   TP.C.watch(true);
  // }

  return t_earliest_feas;
}

TaskPart plan_in_animation_komo(TimedConfigurationProblem &TP,
                                const uint t0, const arr &q0, const arr &q1,
                                const uint time_lb, const Robot prefix,
                                const int time_ub_prev_found = -1) {
  // return TaskPart();

  // Check if start q is feasible
  const auto start_res = TP.query(q0, t0);
  // if (!start_res->isFeasible && min(start_res->coll_y) < -0.1) {
  if (!start_res->isFeasible) {
    // std::cout << t0 << std::endl;
    // LOG(-1) << "q_start is not feasible! This should not happen ";
    spdlog::error("q_start is not feasible at time {}! This should not happen", t0);
    
    if (start_res->coll_y.N > 0){
      spdlog::error("penetration is {}", min(start_res->coll_y));
    }

    start_res->writeDetails(cout, TP.C);
    // std::cout << "colliding by " << min(start_res->coll_y) << std::endl;

    TP.A.setToTime(TP.C, t0);
    TP.C.setJointState(q0);
    // TP.C.watch(true);

    return TaskPart();
  }

  // const uint dt_max_vel = uint(std::ceil(length(q0 - q1) / prefix.vmax));
  const uint dt_max_vel = uint(std::ceil(absMax(q0 - q1) / prefix.vmax));

  // the goal should always be free at the end of the animation, as we always
  // plan an exit path but it can be the case that we are currently planning an
  // exit path, thus we include the others
  const uint t_max_to_check = std::max({time_lb, t0 + dt_max_vel, TP.A.getT()});
  // establish time at which the goal is free, and stays free
  const double t_earliest_feas = get_earliest_feasible_time(
      TP, q1, t_max_to_check, std::max({time_lb, t0 + dt_max_vel}));

  spdlog::info("Final time for komo: {}, dt: ", t_earliest_feas, t_earliest_feas - t0);
  // std::cout << "Final time for komo: " << t_earliest_feas
  //           << " dt: " << t_earliest_feas - t0 << std::endl;

  // the minimum horizon length is mainly given by the earliest possible finish
  // time
  const uint min_horizon_length =
      std::max({5u, dt_max_vel, uint(t_earliest_feas - t0)});

  uint horizon = std::ceil(min_horizon_length);

  const uint max_komo_run_attempts = 3;
  uint iters = 0;
  while (true) {
    spdlog::info("running komo with horizon {}", horizon);
    arr ts(horizon);
    for (uint j = 0; j < horizon; ++j) {
      ts(j) = t0 + j;
    }

    if (time_ub_prev_found > 0 && time_ub_prev_found < ts(-1)) {
      spdlog::info("found cheaper path before, aborting.");
      return TaskPart();
    }

    // ensure that the goal is truly free. Sanity check.
    const auto res = TP.query(q1, t0 + horizon);
    if (!res->isFeasible) {
      spdlog::error("Goal is not valid.");
      res->writeDetails(cout, TP.C);
      
      // TP.A.setToTime(TP.C, t0 + horizon);
      // TP.C.setJointState(q1);
      // TP.C.watch(true);

      return TaskPart();
    }

    if (false) {
      TP.A.setToTime(TP.C, t0);
      TP.C.setJointState(q0);
      TP.C.watch(true);

      TP.A.setToTime(TP.C, t0 + horizon);
      TP.C.setJointState(q1);
      TP.C.watch(true);
    }

    double ineq = 0;
    double eq = 0;
    const arr path =
        plan_with_komo_given_horizon(TP.A, TP.C, q0, q1, ts, prefix, ineq, eq);

    if (path.d0 == 0){
      return TaskPart();
    }

    if (false) {
      for (uint i = 0; i < ts.N; ++i) {
        TP.A.setToTime(TP.C, ts(i));
        TP.C.setJointState(path[i]);
        TP.C.watch(true);
      }
    }

    if (false) {
      for (uint i = 0; i < ts.N; ++i) {
        const auto res = TP.query(path[i], ts(i));
        if (!res->isFeasible) {
          TP.A.setToTime(TP.C, ts(i));
          TP.C.setJointState(path[i]);
          TP.C.watch(true);
        }
      }
    }

    // check max. speed
    const double max_speed = get_max_speed(path);
    spdlog::info("Komo max speed: {}", max_speed);

    // if the violations are this high, we give up
    if (eq > 15 && ineq > 15) {
      return TaskPart();
    }

    if (max_speed <= prefix.vmax && eq < 1.5 && ineq < 1.5) {
      spdlog::info("done, found a komo solution with ineq {}, and eq {}", ineq, eq);
      return TaskPart(ts, path);
    }

    if (iters >= max_komo_run_attempts) {
      spdlog::info("Stopping komo attempts, too many attempts. ineq {}, eq {}", ineq, eq);
      return TaskPart();
    }

    // const uint num_add_timesteps = std::max({uint((iters+1)*2), uint(req_t -
    // ts.N)}); const uint num_add_timesteps = std::ceil(1. *
    // (max_horizon_length
    // - min_horizon_length) / max_komo_run_attempts);
    const int add_timesteps_for_speed =
        std::max({0, int(std::ceil(horizon * ((max_speed / prefix.vmax) - 1)))});
    const uint num_add_timesteps =
        std::max({(iters + 1) * 3u, uint(add_timesteps_for_speed)});
        spdlog::info("adding {} timesteps to decrease the maximum speed", num_add_timesteps);

    if (num_add_timesteps > 300){
      spdlog::info("komo path too long, aborting.");;
      return TaskPart();
    }

    horizon += num_add_timesteps;

    spdlog::info("rerunning komo, current violation: ineq {}, eq {}", ineq, eq);

    ++iters;
  }
}

TaskPart plan_in_animation_rrt(TimedConfigurationProblem &TP,
                               const uint t0, const arr &q0, const arr &q1,
                               const uint time_lb, const Robot prefix,
                               int time_ub_prev_found = -1) {
  // TimedConfigurationProblem TP(C, A);
  // deleteUnnecessaryFrames(TP.C);
  // const auto pairs = get_cant_collide_pairs(TP.C);
  // TP.C.fcl()->deactivatePairs(pairs);
  // TP.activeOnly = true;

  // Check if start q is feasible
  const auto start_res = TP.query(q0, t0);
  if (!start_res->isFeasible) {
    // TP.C.watch(true);

    spdlog::error("q_start is not feasible at time {}! This should not happen", t0);
    // std::cout << q0 << std::endl;
    start_res->writeDetails(cout, TP.C);
    if (start_res->coll_y.N > 0){
      spdlog::error("penetration is {}", min(start_res->coll_y));
    }

    // for (uint i=0; i<TP.A.getT(); ++i){
    //   std::cout << i << std::endl;
    //   TP.A.setToTime(TP.C, i);
    //   //TP.C.setJointState(q1);
    //   TP.C.watch(true);
    // }

    // std::cout << t0 << std::endl;
    // LOG(-1) << "q_start is not feasible! This should not happen ";
    // std::cout << "colliding by " << min(start_res->coll_y) << std::endl;

    // TP.A.setToTime(TP.C, t0);
    // TP.C.setJointState(q0);
    // TP.C.watch(true);

    return TaskPart();
  }

  // if(TP.A.prePlannedFrames.N > 0){
    // for (uint i = t0; i < time_lb; ++i){
    //   const auto res = TP.query(q0, i);
    //   std::cout << res->isFeasible << std::endl;
    //   TP.C.watch(true);
    // }
  // }

  // TP.A.setToTime(TP.C, t0);
  // TP.C.setJointState(q1);
  // TP.C.watch(true);


  bool sipp = true;

  if (sipp)
  {
    


    const uint dt_max_vel = uint(std::ceil(absMax(q0 - q1) / prefix.vmax));
    // const uint dt_max_vel = uint(std::ceil(length(q0 - q1) / prefix.vmax));

    // the goal should always be free at the end of the animation, as we always
    // plan an exit path but it can be the case that we are currently planning an
    // exit path, thus we include the others
    const uint t_max_to_check = std::max({time_lb, t0 + dt_max_vel, TP.A.getT()});
    // establish time at which the goal is free, and stays free
    const uint t_earliest_feas = get_earliest_feasible_time(
        TP, q1, t_max_to_check, std::max({time_lb, t0 + dt_max_vel}));

    spdlog::info("t_earliest_feas {}", t_earliest_feas);
    spdlog::info("last anim time {}", TP.A.getT());
    // std::cout << "t_earliest_feas " << t_earliest_feas << std::endl;
    // std::cout << "last anim time " << TP.A.getT() << std::endl;

    if (false) {
      TP.A.setToTime(TP.C, TP.A.getT());
      TP.C.setJointState(q1);
      TP.C.watch(true);
    }

    // run once without upper bound
    // auto res_check_feasibility = planner.plan(q0, t0, q1, t_earliest_feas);

 

    double total_rrt_time = 0;
    double total_nn_time = 0;
    double total_coll_time = 0;

    const uint max_delta = 10;
    const uint max_iter = 10;
    TimedPath timedPath({}, {});
    for (uint i = 0; i < max_iter; ++i) {
      const uint time_ub = t_earliest_feas + max_delta * (i);

      spdlog::info("RRT iteration {}, upper bound time {}", i, time_ub);
      if (time_ub_prev_found > 0 && time_ub >= uint(time_ub_prev_found)) {
        spdlog::info("Aborting bc. faster path found");
        break;
      }

      const auto rrt_start_time = std::chrono::high_resolution_clock::now();
      PathFinder_SIRRT_Time planner(TP);
      if (TP.A.prePlannedFrames.N != 0){
      planner.prePlannedFrames = TP.A.prePlannedFrames;
      planner.tPrePlanned = TP.A.tPrePlanned;
      }
      auto res = planner.plan(q0, t0, q1, time_ub);
      
      const auto rrt_end_time = std::chrono::high_resolution_clock::now();
      const auto rrt_duration =
          std::chrono::duration_cast<std::chrono::microseconds>(rrt_end_time -
                                                                rrt_start_time)
              .count();

      total_rrt_time += rrt_duration;
      total_coll_time += planner.edge_checking_time_us;
      total_nn_time += planner.nn_time_us;

      if (res.time.N != 0) {
        timedPath = res;
        break;
      }
    }

    if (timedPath.time.N == 0) {
      TaskPart tp;
      tp.stats.rrt_plan_time = total_rrt_time;
      tp.stats.rrt_nn_time = total_nn_time;
      tp.stats.rrt_coll_time = total_coll_time;

      return tp;
    }
    //HERE STAAAAAART
    

    // resample
    const uint N = std::ceil(timedPath.time(timedPath.time.N - 1) - t0) + 1;
    arr t(N);
    for (uint i = 0; i < t.N; ++i) {
      t(i) = i + t0;
    }

    // std::cout << timedPath.time(-1) << " " <<  t(0) << " " << t(t.N-1) << std::endl;

    const arr path = timedPath.resample(t, TP.C);

    {
      const double max_speed = get_max_speed(path);
      spdlog::info("RRT maximum speed after resampling {}", max_speed);
    }

    // check if resampled path is still fine
    for (uint i = 0; i < t.N; ++i) {
      const auto res = TP.query(path[i], t(i));
      if (!res->isFeasible) {
        spdlog::error("resampled path is not feasible! This should not happen.");
        start_res->writeDetails(cout, TP.C);
      }
    }

    // shortcutting
    // TODO: add timing
    const auto shortcut_start_time = std::chrono::high_resolution_clock::now();
    const bool should_shortcut = rai::getParameter<bool>("shortcutting", true);

    arr new_path = path;
    if (should_shortcut && path.d0 > 2){
      spdlog::info("Running shortcutter");
      // for (uint i = 0; i < path.d0; ++i) {
      //   const auto res = TP.query(path[i], t(i));
      //   std::cout << "checking at time " << t(i) << std::endl;
      //   if (t(i) == 70){
      //     TP.C.watch(true);
      //   }
      //   if (!res->isFeasible) {
      //     // std::cout << i << std::endl;
      //     // TP.C.watch(true);
      //     spdlog::warn("pre shortcut path infeasible, penetration {} at time {} "
      //                  "(timestep {} / {})",
      //                  min(res->coll_y), t(i), i, new_path.d0);
      //     res->writeDetails(std::cout, TP.C);
      //     break;
      //   }
      // }

      new_path = partial_spacetime_shortcut(TP, path, t0);

      for (uint i = 0; i < new_path.d0; ++i) {
        const auto res = TP.query(new_path[i], t(i));
        if (!res->isFeasible) {
          // std::cout << i << std::endl;
          // TP.C.watch(true);
          res->writeDetails(std::cout, TP.C);
          if (res->coll_y.N > 0){
            spdlog::warn("shortcut path infeasible, penetration {} at time {} "
                        "(timestep {} / {})",
                        min(res->coll_y), t(i), i, new_path.d0);
          }
          break;
        }
      }

      const double max_speed = get_max_speed(new_path);
      spdlog::info("RRT maximum speed after shortcutting {}", max_speed);
    }
    // const arr new_path = path;

    const auto shortcut_end_time = std::chrono::high_resolution_clock::now();
    const auto shortcut_duration =
        std::chrono::duration_cast<std::chrono::microseconds>(shortcut_end_time -
                                                              shortcut_start_time)
            .count();

    // std::cout << new_path[-1] << "\n" << q1 << std::endl;

    // smoothing and imposing bunch of constraints
    const auto smoothing_start_time = std::chrono::high_resolution_clock::now();
    
    // TP.C.fcl()->stopEarly = false;

    const bool should_smooth = rai::getParameter<bool>("smoothing", false);

    arr smooth_path = new_path * 1.;
    if (should_smooth){
      spdlog::info("Running smoother");
      smooth_path = smoothing(TP.A, TP.C, t, new_path, prefix.prefix);
      
      if (smooth_path.N == 0){
        spdlog::info("Smoothing failed.");
        smooth_path = new_path;
      }
      else{
        for (uint i = 0; i < smooth_path.d0; ++i) {
          const auto res = TP.query(smooth_path[i], t(i));
          // if (!res->isFeasible && res->coll_y.N > 0 && min(res->coll_y) < -0.05) {
          // if (i < smooth_path.d0 -1){
          //   if (absMax(smooth_path[i] - smooth_path[i+1]) > prefix.vmax){
          //     spdlog::warn("smoothed path too fast {} vs {}.",
          //                  absMax(smooth_path[i] - smooth_path[i + 1]),
          //                  prefix.vmax);
          //     smooth_path = new_path;
          //     break;
          //   }
          // }
          // std::cout << "checking at time " << t(i) << std::endl;
          if (!res->isFeasible) {
            // std::cout << i << std::endl;
            // TP.C.watch(true);
            spdlog::warn(
                "smoothed path infeasible, penetration {} at time {} (timestep {} / {})",
                min(res->coll_y), t(i), i, smooth_path.d0);
            res->writeDetails(std::cout, TP.C);

            smooth_path = new_path;
            break;
          }
        }
      }

      const double max_speed = get_max_speed(smooth_path);
      spdlog::info("RRT maximum speed after smoothing {}", max_speed);

    }
    // TP.C.fcl()->stopEarly = true;
    const auto smoothing_end_time = std::chrono::high_resolution_clock::now();
    const auto smoothing_duration =
        std::chrono::duration_cast<std::chrono::microseconds>(smoothing_end_time -
                                                              smoothing_start_time)
            .count();

    // const arr smooth_path = new_path;

    const double max_speed = get_max_speed(smooth_path);
    spdlog::info("RRT maximum speed {}", max_speed);
    spdlog::info("RRT final time at {}", t(-1));
    //// END    
    TaskPart tp(t, smooth_path);
    tp.stats.rrt_plan_time = total_rrt_time;
    tp.stats.rrt_nn_time = total_nn_time;
    tp.stats.rrt_coll_time = total_coll_time;
    tp.stats.rrt_shortcut_time = shortcut_duration;
    tp.stats.rrt_smoothing_time = smoothing_duration;

    return tp;
  }

  else
  {
    PathFinder_RRT_Time planner(TP);
    planner.vmax = prefix.vmax;
    planner.lambda = 0.5;
    planner.maxIter = 500;
    planner.maxInitialSamples = 10;
    // planner.disp = true;
    // planner.optimize = optimize;
    // planner.step_time = 5;
    planner.maxIter = 500;
    planner.goalSampleProbability = 0.9; // 0.9
    const bool informed_sampling = rai::getParameter<bool>("informed_sampling", true);
    planner.informed_sampling = informed_sampling;

     const uint dt_max_vel = uint(std::ceil(absMax(q0 - q1) / prefix.vmax));
      // const uint dt_max_vel = uint(std::ceil(length(q0 - q1) / prefix.vmax));

      // the goal should always be free at the end of the animation, as we always
      // plan an exit path but it can be the case that we are currently planning an
      // exit path, thus we include the others
      const uint t_max_to_check = std::max({time_lb, t0 + dt_max_vel, TP.A.getT()});
      // establish time at which the goal is free, and stays free
      const uint t_earliest_feas = get_earliest_feasible_time(
          TP, q1, t_max_to_check, std::max({time_lb, t0 + dt_max_vel}));

      spdlog::info("t_earliest_feas {}", t_earliest_feas);
      spdlog::info("last anim time {}", TP.A.getT());
      // std::cout << "t_earliest_feas " << t_earliest_feas << std::endl;
      // std::cout << "last anim time " << TP.A.getT() << std::endl;

      if (false) {
        TP.A.setToTime(TP.C, TP.A.getT());
        TP.C.setJointState(q1);
        TP.C.watch(true);
      }

      // run once without upper bound
      // auto res_check_feasibility = planner.plan(q0, t0, q1, t_earliest_feas);

      if (TP.A.prePlannedFrames.N != 0){
        planner.prePlannedFrames = TP.A.prePlannedFrames;
        planner.tPrePlanned = TP.A.tPrePlanned;
      }

      double total_rrt_time = 0;
      double total_nn_time = 0;
      double total_coll_time = 0;

      const uint max_delta = 10;
      const uint max_iter = 10;
      TimedPath timedPath({}, {});
      for (uint i = 0; i < max_iter; ++i) {
        const uint time_ub = t_earliest_feas + max_delta * (i);

        spdlog::info("RRT iteration {}, upper bound time {}", i, time_ub);
        if (time_ub_prev_found > 0 && time_ub >= uint(time_ub_prev_found)) {
          spdlog::info("Aborting bc. faster path found");
          break;
        }

        const auto rrt_start_time = std::chrono::high_resolution_clock::now();
        auto res = planner.plan(q0, t0, q1, t_earliest_feas, time_ub);
        
        const auto rrt_end_time = std::chrono::high_resolution_clock::now();
        const auto rrt_duration =
            std::chrono::duration_cast<std::chrono::microseconds>(rrt_end_time -
                                                                  rrt_start_time)
                .count();

        total_rrt_time += rrt_duration;
        total_coll_time += planner.edge_checking_time_us;
        total_nn_time += planner.nn_time_us;

        if (res.time.N != 0) {
          timedPath = res;
          break;
        }
      }

      if (timedPath.time.N == 0) {
        TaskPart tp;
        tp.stats.rrt_plan_time = total_rrt_time;
        tp.stats.rrt_nn_time = total_nn_time;
        tp.stats.rrt_coll_time = total_coll_time;

        return tp;
      }

      // resample
      const uint N = std::ceil(timedPath.time(timedPath.time.N - 1) - t0) + 1;
      arr t(N);
      for (uint i = 0; i < t.N; ++i) {
        t(i) = i + t0;
      }

      // std::cout << timedPath.time(-1) << " " <<  t(0) << " " << t(t.N-1) << std::endl;

      const arr path = timedPath.resample(t, TP.C);

      {
        const double max_speed = get_max_speed(path);
        spdlog::info("RRT maximum speed after resampling {}", max_speed);
      }

      // check if resampled path is still fine
      for (uint i = 0; i < t.N; ++i) {
        const auto res = TP.query(path[i], t(i));
        if (!res->isFeasible) {
          spdlog::error("resampled path is not feasible! This should not happen.");
          start_res->writeDetails(cout, TP.C);
        }
      }

      // shortcutting
      // TODO: add timing
      const auto shortcut_start_time = std::chrono::high_resolution_clock::now();
      const bool should_shortcut = rai::getParameter<bool>("shortcutting", true);

      arr new_path = path;
      if (should_shortcut && path.d0 > 2){
        spdlog::info("Running shortcutter");
        // for (uint i = 0; i < path.d0; ++i) {
        //   const auto res = TP.query(path[i], t(i));
        //   std::cout << "checking at time " << t(i) << std::endl;
        //   if (t(i) == 70){
        //     TP.C.watch(true);
        //   }
        //   if (!res->isFeasible) {
        //     // std::cout << i << std::endl;
        //     // TP.C.watch(true);
        //     spdlog::warn("pre shortcut path infeasible, penetration {} at time {} "
        //                  "(timestep {} / {})",
        //                  min(res->coll_y), t(i), i, new_path.d0);
        //     res->writeDetails(std::cout, TP.C);
        //     break;
        //   }
        // }

        new_path = partial_spacetime_shortcut(TP, path, t0);

        for (uint i = 0; i < new_path.d0; ++i) {
          const auto res = TP.query(new_path[i], t(i));
          if (!res->isFeasible) {
            // std::cout << i << std::endl;
            // TP.C.watch(true);
            res->writeDetails(std::cout, TP.C);
            if (res->coll_y.N > 0){
              spdlog::warn("shortcut path infeasible, penetration {} at time {} "
                          "(timestep {} / {})",
                          min(res->coll_y), t(i), i, new_path.d0);
            }
            break;
          }
        }

        const double max_speed = get_max_speed(new_path);
        spdlog::info("RRT maximum speed after shortcutting {}", max_speed);
      }
      // const arr new_path = path;

      const auto shortcut_end_time = std::chrono::high_resolution_clock::now();
      const auto shortcut_duration =
          std::chrono::duration_cast<std::chrono::microseconds>(shortcut_end_time -
                                                                shortcut_start_time)
              .count();

      // std::cout << new_path[-1] << "\n" << q1 << std::endl;

      // smoothing and imposing bunch of constraints
      const auto smoothing_start_time = std::chrono::high_resolution_clock::now();
      
      // TP.C.fcl()->stopEarly = false;

      const bool should_smooth = rai::getParameter<bool>("smoothing", false);

      arr smooth_path = new_path * 1.;
      if (should_smooth){
        spdlog::info("Running smoother");
        smooth_path = smoothing(TP.A, TP.C, t, new_path, prefix.prefix);
        
        if (smooth_path.N == 0){
          spdlog::info("Smoothing failed.");
          smooth_path = new_path;
        }
        else{
          for (uint i = 0; i < smooth_path.d0; ++i) {
            const auto res = TP.query(smooth_path[i], t(i));
            // if (!res->isFeasible && res->coll_y.N > 0 && min(res->coll_y) < -0.05) {
            // if (i < smooth_path.d0 -1){
            //   if (absMax(smooth_path[i] - smooth_path[i+1]) > prefix.vmax){
            //     spdlog::warn("smoothed path too fast {} vs {}.",
            //                  absMax(smooth_path[i] - smooth_path[i + 1]),
            //                  prefix.vmax);
            //     smooth_path = new_path;
            //     break;
            //   }
            // }
            // std::cout << "checking at time " << t(i) << std::endl;
            if (!res->isFeasible) {
              // std::cout << i << std::endl;
              // TP.C.watch(true);
              spdlog::warn(
                  "smoothed path infeasible, penetration {} at time {} (timestep {} / {})",
                  min(res->coll_y), t(i), i, smooth_path.d0);
              res->writeDetails(std::cout, TP.C);

              smooth_path = new_path;
              break;
            }
          }
        }

        const double max_speed = get_max_speed(smooth_path);
        spdlog::info("RRT maximum speed after smoothing {}", max_speed);

      }
      // TP.C.fcl()->stopEarly = true;
      const auto smoothing_end_time = std::chrono::high_resolution_clock::now();
      const auto smoothing_duration =
          std::chrono::duration_cast<std::chrono::microseconds>(smoothing_end_time -
                                                                smoothing_start_time)
              .count();

      // const arr smooth_path = new_path;

      const double max_speed = get_max_speed(smooth_path);
      spdlog::info("RRT maximum speed {}", max_speed);
      spdlog::info("RRT final time at {}", t(-1));
      
      TaskPart tp(t, smooth_path);
      tp.stats.rrt_plan_time = total_rrt_time;
      tp.stats.rrt_nn_time = total_nn_time;
      tp.stats.rrt_coll_time = total_coll_time;
      tp.stats.rrt_shortcut_time = shortcut_duration;
      tp.stats.rrt_smoothing_time = smoothing_duration;

      return tp;
      }
}

// policy should have other inputs:
// policy(start_pos, end_pos, robot, mode)
void run_waiting_policy(TaskPart &path, const uint lower = 5, const uint upper = 15){
  // TODO: fix distribution
  const uint wait_time = rand() % (upper - lower) + lower;
    for (uint i=0; i<wait_time; ++i){
      path.path.append(path.path[-1]);
      path.t.append(path.t(-1) + 1);
    }
}

// TODO:
// - needs a way to represent constraints
// - collection of objectives?
// - mode represents constraints?
TaskPart plan_in_animation_constrained(){
  // run komo planner with constraints
  return {};
}

// TODO: remove prefix from here
// TODO: add mode-argument
// robust 'time-optimal' planning method
TaskPart plan_in_animation(TimedConfigurationProblem &TP,
                           const uint t0, const arr &q0, const arr &q1,
                           const uint time_lb, const Robot r,
                           const bool exit_path) {
  const auto start_time = std::chrono::high_resolution_clock::now();

  // rai::Configuration CPlan = C;
  // deleteUnnecessaryFrames(CPlan);

  // TimedConfigurationProblem TP(C, A);
  // // TP.C.self->fcl = C.self->fcl;
  // // TP.C.copy(C, true);
  // // deleteUnnecessaryFrames(TP.C);
  // const auto pairs = get_cant_collide_pairs(TP.C);
  // TP.C.fcl()->deactivatePairs(pairs);
  // // TP.C.fcl()->stopEarly = true;
  // TP.activeOnly = true;

  // run rrt
  // TP.C.fcl()->stopEarly = false;

  TaskPart rrt_path = plan_in_animation_rrt(TP, t0, q0, q1, time_lb, r);
  rrt_path.algorithm = "rrt";

  // add waiting times for grabbing
  // TODO: sample from actual ststistical model
  // TODO: should be a policy that does the final movement
  if (global_params.randomize_mod_switch_durations && !exit_path && rrt_path.t.d0 > 0){
    run_waiting_policy(rrt_path);
  }

  // TP.C.fcl()->stopEarly = false;

  const auto rrt_end_time = std::chrono::high_resolution_clock::now();
  const auto rrt_duration =
      std::chrono::duration_cast<std::chrono::microseconds>(rrt_end_time -
                                                            start_time)
          .count();

  // if (rrt_path.has_solution){
  //   TP.query(rrt_path.path[-1], rrt_path.t(-1));
  //   TP.C.watch(true);
  // }

  /*if(rrt_path.has_solution){
    TimedConfigurationProblem TP(C, A);
    for (uint i = 0; i < rrt_path.t.N; ++i) {
      const auto res = TP.query(rrt_path.path[i], rrt_path.t(i));
      if(res->coll_y.N > 0) std::cout <<min(res->coll_y) << std::endl;
      if (!res->isFeasible){
        std::cout << "B" << std::endl;
        std::cout << "B" << std::endl;
        std::cout << "B" << std::endl;
      }
      if (!res->isFeasible && min(res->coll_y) < -0.01) {
        std::cout << "rrt actually infeasible" << std::endl;
        rrt_path.has_solution = false;
        break;
      }
    }
  }*/

  int time_ub = -1;
  if (rrt_path.has_solution) {
    time_ub = rrt_path.t(-1);
  }

  // attempt komo
  const auto komo_start_time = std::chrono::high_resolution_clock::now();
  const bool attempt_komo_planning = rai::getParameter<bool>("attempt_komo", true);
  
  TaskPart komo_path;
  if (attempt_komo_planning){
    komo_path =
        plan_in_animation_komo(TP, t0, q0, q1, time_lb, r, time_ub);
    komo_path.algorithm = "komo";

    /*if(komo_path.has_solution){
      return komo_path;
    }*/

    // ensure that the komo-path does not run into start configurations of future
    // tasks
    if (komo_path.has_solution) {
      // TimedConfigurationProblem TP(C, A);
      // deleteUnnecessaryFrames(TP.C);
      // const auto pairs = get_cant_collide_pairs(TP.C);
      // TP.C.fcl()->deactivatePairs(pairs);
      // TP.activeOnly = true;

      spdlog::info("Checking komo path for colisions");
      for (uint i = 0; i < komo_path.t.N; ++i) {
        const auto res = TP.query(komo_path.path[i], komo_path.t(i));
        if (!res->isFeasible){
          spdlog::warn("komo path is colliding, penetrating {}", min(res->coll_y));
        }
        // if (!res->isFeasible && min(res->coll_y) < -0.01) {
        if (!res->isFeasible) {
          spdlog::warn("komo actually infeasible");
          komo_path.has_solution = false;
          break;
        }
      }
    }
  }

  const auto komo_end_time = std::chrono::high_resolution_clock::now();
  const auto komo_duration =
      std::chrono::duration_cast<std::chrono::microseconds>(komo_end_time -
                                                            komo_start_time)
          .count();

  const auto end_time = std::chrono::high_resolution_clock::now();
  const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                            end_time - start_time)
                            .count();

  rrt_path.stats.total_compute_time = duration;
  komo_path.stats.total_compute_time = duration;

  rrt_path.stats.rrt_compute_time = rrt_duration;
  komo_path.stats.rrt_compute_time = rrt_duration;

  rrt_path.stats.komo_compute_time = komo_duration;
  komo_path.stats.komo_compute_time = komo_duration;

  // add rrt-stats
  komo_path.stats.rrt_nn_time = rrt_path.stats.rrt_nn_time;
  komo_path.stats.rrt_coll_time = rrt_path.stats.rrt_coll_time;
  komo_path.stats.rrt_smoothing_time = rrt_path.stats.rrt_smoothing_time;
  komo_path.stats.rrt_shortcut_time = rrt_path.stats.rrt_shortcut_time;
  komo_path.stats.rrt_plan_time = rrt_path.stats.rrt_plan_time;

  if (komo_path.has_solution && !rrt_path.has_solution) {
    spdlog::info("Using KOMO");
    return komo_path;
  }
  if (!komo_path.has_solution && rrt_path.has_solution) {
    spdlog::info("Using RRT");
    return rrt_path;
  }

  if (komo_path.has_solution && rrt_path.has_solution &&
      komo_path.t(-1) < rrt_path.t(-1)) {
    spdlog::info("Using KOMO, as the path is shorter");
    return komo_path;
  }

  if (rrt_path.has_solution){
    spdlog::info("Using RRT as default");
    return rrt_path;
  }

  return TaskPart();
}

class PrioritizedTaskPlanner {
  public:
    std::unordered_map<Robot, arr> home_poses;
    RobotTaskPoseMap rtpm;

    uint best_makespan_so_far;
    bool early_stopping;

    // swap to goal sampler not precomputed goal poses
    PrioritizedTaskPlanner(const std::unordered_map<Robot, arr> &_home_poses,
                const RobotTaskPoseMap &_rtpm, const uint _best_makespan_so_far,
                const bool _early_stopping)
        : home_poses(_home_poses), rtpm(_rtpm),
          best_makespan_so_far(_best_makespan_so_far),
          early_stopping(_early_stopping) {}

    // TODO: swap to std::map<Robot, uint> finishing_times;
    PlanStatus plan(rai::Configuration &C, const RobotTaskPair &rtp,
                    const uint prev_finishing_time, Plan &paths) {
      rai::Animation tmp;
      TimedConfigurationProblem TP(C, tmp);

      const auto pairs = get_cant_collide_pairs(TP.C);
      TP.C.fcl()->deactivatePairs(pairs);
      TP.C.fcl()->stopEarly = global_params.use_early_coll_check_stopping;
      TP.activeOnly = true;

      rai::Configuration &CPlanner = TP.C;

      // set robots to home pose
      // for (const auto &r : home_poses) {
      //   setActive(CPlanner, r.first);
      //   CPlanner.setJointState(r.second);
      // }

      std::unordered_map<Robot, FrameL> robot_frames;
      for (auto r : home_poses) {
        const auto robot = r.first;
        robot_frames[robot] = get_robot_frames(CPlanner, robot);
      }

      if (rtp.task.type == PrimitiveType::handover){
        // r1 [ pick ] [handover] [ exit ]
        // r2  xx ] [  move to  ] [  place  ] [exit]
        const auto r1 = rtp.robots[0];
        const auto r2 = rtp.robots[1];
        spdlog::info("Planning handover for robots {}, {}", r1.prefix, r2.prefix);

        // plan pick for r1
        {
          bool removed_exit_path = false;
          const uint max_start_time_shift = 0;
          if (paths.count(r1) > 0 && paths[r1].back().is_exit &&
              prev_finishing_time <
                  paths[r1].back().t(-1) + 1 + max_start_time_shift) {
            spdlog::info("Removing exit path of robot {}", r1.prefix);
            spdlog::info("exit path end time: {}", paths[r1].back().t(-1));

            paths[r1].pop_back();
            removed_exit_path = true;

            std::cout << "A" << std::endl;
            int path_end_time = 0;
            if (paths[r1].size() > 0){
              path_end_time = paths[r1].back().t(-1);
            }
            spdlog::info("New end time: {}", path_end_time);
          }

          rai::Animation A = make_animation_from_plan(paths);

          A.setToTime(CPlanner, prev_finishing_time);

          if (false) {
            for (uint i = 0; i < A.getT(); ++i) {
              A.setToTime(CPlanner, i);
              CPlanner.watch(false);
              rai::wait(0.05);
            }
          }

          // set configuration to plannable for current robot
          spdlog::info("Setting up configuration for robot {}", r1.prefix);
          setActive(CPlanner, r1);
          TP.A = A;

          const arr pick_start_pose = (removed_exit_path && paths[r1].size() > 0) ? paths[r1].back().path[-1]: CPlanner.getJointState();
          const arr pick_pose = rtpm[rtp][0][0];

          // ensure that the start time is not too far away from the earliest end time:
          uint pick_start_time = (paths.count(r1) > 0 && paths[r1].size() > 0) ? paths[r1].back().t(-1): 0;

          if (removed_exit_path) {
            // if we removed the exit path, we always need to start planning from that time.
            // If we do not, other plans might become infeasible
            if (paths[r1].size() > 0){
              pick_start_time = paths[r1].back().t(-1);}
            else{
              pick_start_time = 0;
            }
          } else {
            // ensure that the difference between the previous finish and the 
            // start time of this plan is not too large.
            // By definition, pick_start_time is smaller that prev_finishing_time
            const uint time_diff = prev_finishing_time - pick_start_time;
            if (time_diff > max_start_time_shift) {
              pick_start_time = prev_finishing_time - max_start_time_shift;
            }
          }

          spdlog::info("Picking start time {}", pick_start_time);

          auto path = plan_in_animation(TP, pick_start_time, pick_start_pose, pick_pose,
                                        0, r1, false);

          if (path.has_solution) {
            if (false) {
              for (uint i = 0; i < path.path.d0; ++i) {
                A.setToTime(CPlanner, path.t(i));
                auto q = path.path[i];
                CPlanner.setJointState(q);
                CPlanner.watch(false);
                rai::wait(0.05);

                auto res = TP.query(q, path.t(i));
                res->writeDetails(std::cout, TP);
              }
              CPlanner.setJointState(home_poses.at(r1));
            }

            // TODO: need to add the whoel thing with animation making etc
            // make animation part
            auto tmp_frames = robot_frames.at(r1);

            // add obj. frame to the anim-part.
            bool tmp = false;
            for (auto a: A.A){
              if (a.frameNames.contains(STRING("obj" << rtp.task.object + 1))){
                tmp = true;
                break;
              }
            }
            if (tmp == false) {
              const auto obj = STRING("obj" << rtp.task.object + 1);
              auto to = CPlanner[obj];
              tmp_frames.append(to);
            }
            
            const auto anim_part =
                make_animation_part(CPlanner, path.path, tmp_frames, pick_start_time);
            path.r = r1;
            path.anim = anim_part;
            path.has_solution = true;
            path.is_exit = false;
            path.name = "pick";
            path.task_index = rtp.task.object;

            if (early_stopping && path.t(-1) > best_makespan_so_far) {
              spdlog::info("Stopping early due to better prev. path. ({})", best_makespan_so_far);
              return PlanStatus::aborted;
            }

            paths[r1].push_back(path);

            // std::cout << path.path << std::endl;

            // debugging: ensure that the goal pose is what we want it to be
            if (path.path[-1] != pick_pose){
              spdlog::info("Pick keyframe is not the same as the one in plan, diff {}.", length(path.path[-1] - pick_pose));
              // std::cout << path.path[-1] << " " << pick_pose << std::endl;
            }
          }
          else{
            return PlanStatus::failed;
          }
        }
        
        // plan handover jointly
        {
          spdlog::info("Planning handover action");

          // bool removed_exit_path = false;
          // const uint max_start_time_shift = 0;
          // if (paths.count(r2) > 0 && paths[r2].back().is_exit &&
          //     prev_finishing_time <
          //         paths[r2].back().t(-1) + 1 + max_start_time_shift) {
          //   std::cout << "removing exit path of " << r2 << std::endl;
          //   std::cout << "exit path end time: " << paths[r2].back().t(-1)
          //             << std::endl;
          //   paths[r2].pop_back();

          //   removed_exit_path = true;
          // }

          // link obj to robot 1
          CPlanner.setJointState(paths[r1].back().path[-1]);
          // const arr pick_pose = rtpm[rtp][0][0];
          // CPlanner.setJointState(pick_pose);
          // CPlanner.watch(true);
          const auto pen_tip = STRING(r1 << r1.ee_frame_name);
          const auto obj = STRING("obj" << rtp.task.object + 1);

          auto from = CPlanner[pen_tip];
          auto to = CPlanner[obj];

          to->unLink();

          // create a new joint
          to->linkFrom(from, true);
          // to->joint->makeRigid();
        
          // std::cout << "A" << std::endl;
          // CPlanner.watch(true);

          rai::Animation A = make_animation_from_plan(paths);

          const uint pick_end_time = (paths.count(r1) > 0) ? paths[r1].back().t(-1): 0;
          const uint other_end_time = (paths.count(r2) > 0) ? paths[r2].back().t(-1): 0;

          uint r1_start_time = pick_end_time;
          uint r2_start_time = other_end_time;

          // const uint start_time = std::max({pick_end_time, other_end_time});

          const uint max_offset = 0;
          if (pick_end_time <= other_end_time && other_end_time - pick_end_time > max_offset){
            r1_start_time = other_end_time - max_offset;            
          }
          else if (pick_end_time >= other_end_time && pick_end_time - other_end_time > max_offset){
            r2_start_time = pick_end_time - max_offset;
          }

          if (paths.count(r1) > 0 && paths[r1].back().t(0) > r2_start_time){
            r2_start_time = paths[r1].back().t(0);
          }

          uint t_lb = std::max({pick_end_time, other_end_time});
          uint start_time = std::min({r1_start_time, r2_start_time});

          // std::cout << "B" << std::endl;
          // for (uint i=0; i<30; ++i){
          //   std::cout << i << std::endl;
          //   A.setToTime(CPlanner, i);
          //   CPlanner.watch(true);
          // }

          // for (uint i: {0, 30}){
          //   std::cout << i << std::endl;
          //   A.setToTime(CPlanner, i);
          //   arr tmp = CPlanner.getJointState() * 0.;
          //   CPlanner.setJointState(tmp);
          //   CPlanner.watch(true);
          // }

          FrameL preplanned;// = agentFrames[agents(0)];

          if (r1_start_time > start_time){
            preplanned = robot_frames[r1];
            preplanned.append(CPlanner[obj]);
          }
          else if (r2_start_time > start_time){
            preplanned = robot_frames[r2];
          }

          // A.prePlannedFrames = preplanned;
          // A.tPrePlanned = t_lb;

          // set configuration to plannable for current robot
          spdlog::info("Setting up configuration and computing times");
          TP.A = A;

          arr handover_start_pose;
          {
            setActive(CPlanner, r2);

            arr pose_r1 = paths[r1].back().path[-1];
            arr pose_r2 = (paths.count(r2)>0 && paths[r2].size()>0) ? paths[r2].back().path[-1] : CPlanner.getJointState();
          
            setActive(CPlanner, r1);
            CPlanner.setJointState(pose_r1);

            setActive(CPlanner, r2);
            CPlanner.setJointState(pose_r2);

            setActive(CPlanner, rtp.robots);
            handover_start_pose = CPlanner.getJointState();
          }
          const arr handover_pose = rtpm[rtp][0][1];

          spdlog::info("Start time at {}", start_time);
          // std::cout << handover_start_pose << std::endl;
          // std::cout << handover_pose << std::endl;

          // update limits
          setActive(CPlanner, rtp.robots);
          TP.limits = CPlanner.getLimits();

          // std::cout << "handover pose" << std::endl;
          // auto res = TP.query(handover_pose, start_time + 30);
          // std::cout << res->isFeasible << std::endl;
          // TP.C.watch(true);

          // for (uint i=0; i<30;++i){
          //   TP.query(handover_pose*0., i);
          //   TP.C.watch(true);
          // }

          // TP.query(handover_pose, 20);
          // TP.C.watch(true);

          // std::cout << TP.C.getJointState() << std::endl;
          
          auto path = plan_in_animation(TP, start_time, handover_start_pose, handover_pose,
                                        t_lb, r1, false);

          if (path.has_solution) {
            if (false) {
              for (uint i = 0; i < path.path.d0; ++i) {
                auto q = path.path[i];
                CPlanner.setJointState(q);
                CPlanner.watch(true);
                rai::wait(0.05);
              }
              setActive(CPlanner, r1);
              CPlanner.setJointState(home_poses.at(r1));
              setActive(CPlanner, r2);
              CPlanner.setJointState(home_poses.at(r2));
              
              setActive(CPlanner, rtp.robots);
            }

            // make animation part
            {
              auto tmp_frames = robot_frames.at(r1);
              // add obj. frame to the anim-part.
              const auto obj = STRING("obj" << rtp.task.object + 1);
              auto to = CPlanner[obj];
              tmp_frames.append(to);
              
              setActive(CPlanner, r1);
              const uint dim = CPlanner.getJointStateDimension();
              setActive(CPlanner, {r1, r2});

              auto r1_joints = get_robot_joints(CPlanner, r1);
              arr r1_joint_path(0, dim);
              arr new_t;
              int ind = r1_start_time - start_time;
              // int ind = 0;
              A.setToTime(CPlanner, path.t(ind));
              for (uint i=ind; i<path.path.d0; ++i){
                CPlanner.setJointState(path.path[i]);
                r1_joint_path.append(CPlanner.getJointState(r1_joints));
                new_t.append(path.t(i));

                // CPlanner.watch(true);
              }

              setActive(CPlanner, r1);
              const auto anim_part =
                  make_animation_part(CPlanner,r1_joint_path, tmp_frames, r1_start_time);
                  // make_animation_part(CPlanner,r1_joint_path, tmp_frames, start_time);
              auto r1_path = TaskPart(new_t, r1_joint_path);

              r1_path.r = r1;
              r1_path.anim = anim_part;
              r1_path.has_solution = true;
              r1_path.is_exit = false;
              r1_path.name = "handover";
              r1_path.task_index = rtp.task.object;

              if (early_stopping && path.t(-1) > best_makespan_so_far) {
                spdlog::info("Stopping early due to better prev. path. ({})", best_makespan_so_far);
                return PlanStatus::aborted;
              }

              paths[r1].push_back(r1_path);
            }
            {
              setActive(CPlanner, rtp.robots);

              auto tmp_frames = robot_frames.at(r2);
              auto r2_joints = get_robot_joints(CPlanner, r2);

              setActive(CPlanner, r2);
              const uint dim = CPlanner.getJointStateDimension();
              setActive(CPlanner, {r1, r2});

              arr r2_joint_path(0, dim);
              arr new_t;
              int ind = r2_start_time - start_time;
              // int ind = 0;
              for (uint i=ind; i<path.path.d0; ++i){
                CPlanner.setJointState(path.path[i]);
                r2_joint_path.append(CPlanner.getJointState(r2_joints));
                new_t.append(path.t(i));
              }

              setActive(CPlanner, r2);

              const auto anim_part =
                  make_animation_part(CPlanner,r2_joint_path, tmp_frames, r2_start_time);
                  // make_animation_part(CPlanner,r2_joint_path, tmp_frames, start_time);
              auto r2_path = TaskPart(new_t, r2_joint_path);
              r2_path.r = r2;
              r2_path.anim = anim_part;
              r2_path.has_solution = true;
              r2_path.is_exit = false;
              r2_path.name = "handover";
              r2_path.task_index = rtp.task.object;

              if (early_stopping && path.t(-1) > best_makespan_so_far) {
                spdlog::info("Stopping early due to better prev. path. ({})", best_makespan_so_far);
                return PlanStatus::aborted;
              }

              paths[r2].push_back(r2_path);
            }
          }
          else{
            return PlanStatus::failed;
          }
        }

        // link obj to r2
        {
          // std::cout << "linky boi" << std::endl;
          // CPlanner.watch(true);
          
          // link obj to robot 1
          setActive(CPlanner, r1);
          CPlanner.setJointState(paths[r1].back().path[-1]);
          setActive(CPlanner, r2);
          CPlanner.setJointState(paths[r2].back().path[-1]);
          
          // CPlanner.watch(true);
          const auto pen_tip = STRING(r2 << r2.ee_frame_name);
          const auto obj = STRING("obj" << rtp.task.object + 1);

          auto from = CPlanner[pen_tip];
          auto to = CPlanner[obj];

          to->unLink();

          // create a new joint
          to->linkFrom(from, true);
        }

        // plan exit path for r1
        {
          // CPlanner.watch(true);

          setActive(CPlanner, r1);
          const uint exit_start_time = paths[r1].back().t(-1);
          const arr exit_path_start_pose = paths[r1].back().path[-1];

          rai::Animation A = make_animation_from_plan(paths);

          if (false) {
            for (uint i = 0; i < A.getT(); ++i) {
              A.setToTime(CPlanner, i);
              CPlanner.watch(false);
              rai::wait(0.1);
            }
          }

          TP.A = A;

          auto exit_path =
              plan_in_animation(TP, exit_start_time, exit_path_start_pose,
                                home_poses.at(r1), exit_start_time, r1, true);

          if (exit_path.has_solution) {
            const auto exit_anim_part = make_animation_part(
                CPlanner, exit_path.path, robot_frames.at(r1), exit_start_time);
           
            exit_path.anim = exit_anim_part;
            exit_path.r = r1;
            exit_path.task_index = 0;
            exit_path.is_exit = true;
            exit_path.name = "exit";

            paths[r1].push_back(exit_path);
          }
          else{
            return PlanStatus::failed;
          }
        }

        // plan place
        {
          // std::cout << "placing" << std::endl;
          // CPlanner.watch(true);

          setActive(CPlanner, r2);
          const uint start_time = paths[r2].back().t(-1);
          const arr start_pose = paths[r2].back().path[-1];

          rai::Animation A = make_animation_from_plan(paths);
          TP.A = A;

          auto path =
              plan_in_animation(TP, start_time, start_pose,
                                rtpm[rtp][0][2], start_time, r2, false);

          if (path.has_solution) {
            if (false) {
              for (uint i = 0; i < path.path.d0; ++i) {
                auto q = path.path[i];
                CPlanner.setJointState(q);
                // auto cp = ConfigurationProblem(CPlanner);
                // auto res = cp.query({}, false);
                // std::cout << res->isFeasible << std::endl;
                CPlanner.watch(true);
                rai::wait(0.05);
              }
              setActive(CPlanner, r2);
              CPlanner.setJointState(home_poses.at(r2));
            }
            auto tmp_frames = robot_frames.at(r2);
            const auto obj = STRING("obj" << rtp.task.object + 1);
            auto to = CPlanner[obj];
            tmp_frames.append(to);  

            const auto anim_part = make_animation_part(
                CPlanner, path.path, tmp_frames, start_time);
          
            path.anim = anim_part;
            path.r = r2;
            path.task_index = rtp.task.object;
            path.is_exit = false;
            path.name = "place";

            paths[r2].push_back(path);
          }
          else{
            return PlanStatus::failed;
          }
        }

        // link to floor
        {            
          setActive(CPlanner, r2);
          CPlanner.setJointState(paths[r2].back().path[-1]);
            // CPlanner.watch(true);
          const auto pen_tip = STRING(r2 << r2.ee_frame_name);
          const auto obj = STRING("obj" << rtp.task.object + 1);

          auto to = CPlanner[obj];
          auto from = CPlanner["table_base"];

          to->unLink();

          // create a new joint
          to->linkFrom(from, true);
            // to->joint->makeRigid();
        }

        // plan exit
        {
          setActive(CPlanner, r2);
          const uint exit_start_time = paths[r2].back().t(-1);
          const arr exit_path_start_pose = paths[r2].back().path[-1];

          rai::Animation A = make_animation_from_plan(paths);

          TP.A = A;

          auto exit_path =
              plan_in_animation(TP, exit_start_time, exit_path_start_pose,
                                home_poses.at(r2), exit_start_time, r1, true);

          if (exit_path.has_solution) {
            const auto exit_anim_part = make_animation_part(
                CPlanner, exit_path.path, robot_frames.at(r2), exit_start_time);

            exit_path.anim = exit_anim_part;
            exit_path.r = r2;
            exit_path.task_index = rtp.task.object;
            exit_path.is_exit = true;
            exit_path.name = "exit";

            paths[r2].push_back(exit_path);
          }
          else{
            return PlanStatus::failed;
          }
        }

        // {
        //   rai::Animation A;
        //   for (const auto &p : paths) {
        //     for (const auto &path : p.second) {
        //       A.A.append(path.anim);
        //     }
        //   }

        //   if (true) {
        //     for (uint i = 0; i < A.getT(); ++i) {
        //       A.setToTime(CPlanner, i);
        //       CPlanner.watch(false);
        //       rai::wait(0.1);
        //     }
        //   }
        // }

        return PlanStatus::success;
      }
      else{
        // plan for current goal
        Robot robot = rtp.robots[0];

        uint lb_from_dependency = 0;
        if (rtp.task.type == PrimitiveType::pick_pick_1){
          spdlog::info("PickPick1");
          robot = rtp.robots[0];
        }
        if (rtp.task.type == PrimitiveType::pick_pick_2) {
          spdlog::info("PickPick2");
          robot = rtp.robots[1];

          // get finishing time of previous robot
          const auto other_robot_primitives = paths[rtp.robots[0]];
          for (const auto &part: other_robot_primitives){
            if (part.task_index == rtp.task.object && !part.is_exit){
              lb_from_dependency = std::max({lb_from_dependency, uint(part.t(-1) + 5)});
            }
          }
        }
        const uint task = rtp.task.object;
        
        spdlog::info("Planning picking for robot {} and object {}", robot.prefix, task);

        // get or compute poses
        const auto poses = rtpm.at(rtp)[0];

        bool is_bin_picking = false;
        if (poses.size() > 1) {
          is_bin_picking = true;
        }

        // remove exit path
        bool removed_exit_path = false;
        const uint max_start_time_shift = 35 * poses.size();
        if (paths[robot].size() > 0 && paths[robot].back().is_exit &&
            prev_finishing_time <
                paths[robot].back().t(-1) + 1 + max_start_time_shift) {
          spdlog::info("Removing exit path of robot {}", robot.prefix);
          spdlog::info("exit path end time: {}", paths[robot].back().t(-1));

          paths[robot].pop_back();
          removed_exit_path = true;

          int new_end_time = 0;
          if (paths[robot].size() > 0){
            new_end_time = paths[robot].back().t(-1);
          }

          spdlog::info("New end time: {}", new_end_time);
        }

        for (uint j = 0; j < poses.size(); ++j) {
          spdlog::info("Planning action {}", j);
          arr start_pose;
          uint start_time;

          if (paths[robot].size() > 0) {
            start_pose = paths[robot].back().path[-1];
            start_time = paths[robot].back().t(-1);
          } else {
            setActive(CPlanner, robot);
            // start_pose = CPlanner.getJointState();
            start_pose = robot.start_pose;
            // CPlanner.watch(true);
            start_time = 0;

            if (prev_finishing_time > max_start_time_shift + 1) {
              start_time = prev_finishing_time - max_start_time_shift + 1;
            }
          }

          if (!removed_exit_path &&
              prev_finishing_time > start_time + max_start_time_shift + 1) {
            start_time = prev_finishing_time - max_start_time_shift + 1;
          }

          const arr goal_pose = poses[j];
          const uint time_lb = std::max(
              {(j == poses.size() - 1) ? prev_finishing_time : 0,
              start_time, 
              lb_from_dependency});

          spdlog::info("Prev. finishing time at {}", prev_finishing_time);
          spdlog::info("New start time at {}", start_time);
          spdlog::info("Lower bound for planning at {}", time_lb);

          // make animation from path-parts
          rai::Animation A = make_animation_from_plan(paths);

          // TP.query(start_pose, start_time);

          // set configuration to plannable for current robot
          spdlog::info("Setting up configuration");
          setActive(CPlanner, robot);
          TP.A = A;

          // if (true) {
          //   for (uint i = 0; i < A.getT() + 10; ++i) {
          //     A.setToTime(CPlanner, i);
          //     CPlanner.watch(false);
          //     rai::wait(0.05);
          //   }
          // }

          // TP.C.setJointState(start_pose);
          // TP.C.watch(true);

          // TP.C.setJointState(start_pose * 0.);
          // TP.C.watch(true);

          // TP.C.setJointState(goal_pose);
          // TP.C.watch(true);

          auto path = plan_in_animation(TP, start_time, start_pose, goal_pose,
                                        time_lb, robot, false);

          path.r = robot;
          path.task_index = task;

          if (is_bin_picking) {
            if (j == 0) {
              path.name = "pick";
            }
            if (j == 1) {
              path.name = "place";
            }
          } else {
            path.name = "goto";
          }

          if (path.has_solution) {
            if (false) {
              for (uint i = 0; i < path.path.d0; ++i) {
                auto q = path.path[i];
                CPlanner.setJointState(q);
                CPlanner.watch(false);
                rai::wait(0.05);
              }
              CPlanner.setJointState(home_poses.at(robot));
            }
            // make animation part
            auto tmp_frames = robot_frames.at(robot);
            // add obj. frame to the anim-part.
            bool tmp = false;
            for (auto a: A.A){
              if (a.frameNames.contains(STRING("obj" << task + 1))){
                tmp = true;
                break;
              }
            }
            if (is_bin_picking && (j == 1 || tmp == false)) {
              const auto obj = STRING("obj" << task + 1);
              auto to = CPlanner[obj];
              tmp_frames.append(to);

              // std::cout << "ADDING OBJ " << task + 1 << " TO ANIM" << std::endl;
            }
            const auto anim_part =
                make_animation_part(CPlanner, path.path, tmp_frames, start_time);
            path.anim = anim_part;

            if (early_stopping && path.t(-1) > best_makespan_so_far) {
              spdlog::info("Stopping early due to better prev. path. ({})", best_makespan_so_far);
              return PlanStatus::aborted;
            }

            paths[robot].push_back(path);
          } else {
            spdlog::info("Was not able to find a path");
            return PlanStatus::failed;
          }

          // re-link things if we are doing bin-picking
          if (is_bin_picking) {
            A.setToTime(CPlanner, path.t(-1));
            CPlanner.setJointState(path.path[-1]);
            const auto pen_tip = STRING(robot << robot.ee_frame_name);
            const auto obj = STRING("obj" << task + 1);

            if (j == 0) {
              auto from = CPlanner[pen_tip];
              auto to = CPlanner[obj];

              to->unLink();

              // create a new joint
              to->linkFrom(from, true);
              // to->joint->makeRigid();
              spdlog::info("Linked obj {} to {}", task + 1, pen_tip.p);
            }

            if (j == 1) {
              auto to = CPlanner[obj];
              auto from = CPlanner["table_base"];

              to->unLink();

              // create a new joint
              to->linkFrom(from, true);
              // to->joint->makeRigid();
              spdlog::info("Linked obj {} to table", task + 1);
            }

            // CPlanner.watch(true);

            // CPlanner.setJointState(CPlanner.getJointState() * 0.);
            // CPlanner.watch(true);
          }
        }

        spdlog::info("Planning exit path.");

        const uint exit_start_time = paths[robot].back().t(-1);
        const arr exit_path_start_pose = paths[robot].back().path[-1];

        rai::Animation A = make_animation_from_plan(paths);

        TP.A = A;

        // if (true) {
        //   for (uint i = 0; i < A.getT(); ++i) {
        //     A.setToTime(CPlanner, i);
        //     CPlanner.watch(false);
        //     rai::wait(0.05);
        //   }
        // }

        auto exit_path =
            plan_in_animation(TP, exit_start_time, exit_path_start_pose,
                              home_poses.at(robot), exit_start_time, robot, true);
        exit_path.r = robot;
        exit_path.task_index = task;
        exit_path.is_exit = true;
        exit_path.name = "exit";

        if (exit_path.has_solution) {
          const auto exit_anim_part = make_animation_part(
              CPlanner, exit_path.path, robot_frames.at(robot), exit_start_time);
          exit_path.anim = exit_anim_part;
          paths[robot].push_back(exit_path);
        } else {
          spdlog::error("Unable to plan an exit path.");
          return PlanStatus::failed;
        }

        return PlanStatus::success;
      }
    }
};

// PlanStatus plan_task(rai::Configuration &C, const RobotTaskPair &rtp,
//                      const RobotTaskPoseMap &rtpm,
//                      const uint best_makespan_so_far,
//                      const std::map<Robot, arr> &home_poses,
//                      const uint prev_finishing_time, const bool early_stopping,
//                      Plan &paths) {
  
// }

PlanResult plan_multiple_arms_given_subsequence_and_prev_plan(
    rai::Configuration C, const RobotTaskPoseMap &rtpm,
    const OrderedTaskSequence &sequence, const uint start_index,
    const Plan prev_plan, const std::unordered_map<Robot, arr> &home_poses,
    const uint best_makespan_so_far = 1e6, const bool early_stopping = false) {
  rai::Configuration CPlanner = C;
  // C.watch(true);

  // prepare planning-configuration
  delete_unnecessary_frames(CPlanner);

  // CPlanner.watch(true);

  std::unordered_map<Robot, FrameL> robot_frames;
  for (auto r : home_poses) {
    const auto robot = r.first;
    robot_frames[robot] = get_robot_frames(CPlanner, robot);
  }

  // remove things from paths
  std::vector<uint> unplanned_tasks;
  for (uint i = start_index; i < sequence.size(); ++i) {
    unplanned_tasks.push_back(sequence[i].task.object);
  }

  std::unordered_map<Robot, std::vector<TaskPart>> paths;

  for (const auto &p : prev_plan) {
    const auto r = p.first;
    for (auto plan : p.second) {
      if (std::find(unplanned_tasks.begin(), unplanned_tasks.end(),
                    plan.task_index) == unplanned_tasks.end()) {
        paths[r].push_back(plan);
        spdlog::info("adding robot {}, plan for object {}", r.prefix, plan.task_index);
      }
    }
  }

  // figure out which robots need an exit path
  std::vector<std::pair<std::string, uint>> robot_exit_paths;
  for (const auto &p : paths) {
    const auto robot = p.first;
    // do not plan an exit path if
    // -- there is already one
    // -- there is no other path
    // -- we are planning for this robot next
    if (p.second.size() > 0 && !paths[robot].back().is_exit) {
      spdlog::info("Need to plan exit path for robot {} at time {}", robot.prefix, paths[robot].back().t(-1));
      robot_exit_paths.push_back({robot.prefix, paths[robot].back().t(-1)});
    }
  }

  // TODO: only do this if the robot is not holding something currently
  // valid home pose is only assumed if the robot is not holding something
  for (const auto &hp: home_poses){
    const auto robot = hp.first;
    // check if we want a path to the home pose at all: 
    // - at the moment, we only do this if we do not hold something.
    bool robot_holds_something = false;
    for (const auto &c: C[STRING(robot.prefix + robot.ee_frame_name)]->children){
      // std::cout << c->name << std::endl;
      if (c->name.contains("obj")){
        robot_holds_something = true;
      }
    }
    if (robot_holds_something){
      continue;
    }

    if (euclideanDistance(robot.start_pose, robot.home_pose) > 1e-6){
      spdlog::info("Planning an exit path for robot {} since it starts not at the home pose", robot.prefix);
      robot_exit_paths.push_back({robot.prefix, 0});
    }
  }

  spdlog::info("Needing to plan {} exit paths.", robot_exit_paths.size());
  std::sort(robot_exit_paths.begin(), robot_exit_paths.end(),
            [](const auto &left, const auto &right) { return left.second < right.second; });

  spdlog::info("tmp.");

  for (const auto &p : robot_exit_paths) {
    Robot robot = home_poses.begin()->first;
    for (const auto &r: home_poses){
      if (r.first.prefix == p.first){
        robot = r.first;
        break;
      }
    }
    // const auto robot = p.first;
    // do not plan an exit path if
    // -- there is already one
    // -- there is no other path
    // -- we are planning for this robot next

    spdlog::info("Planning exit path for robot {} with start time {}", robot.prefix, p.second);

    // plan exit path for robot
    rai::Animation A = make_animation_from_plan(paths);

    setActive(CPlanner, robot);
    TimedConfigurationProblem TP(CPlanner, A);

    const auto pairs = get_cant_collide_pairs(TP.C);
    TP.C.fcl()->deactivatePairs(pairs);
    TP.C.fcl()->stopEarly = global_params.use_early_coll_check_stopping;

    arr start_pose = robot.start_pose;
    int task_index = 0;

    if (paths.count(robot) > 0){
      start_pose = paths[robot].back().path[-1];
      task_index = paths[robot].back().task_index;
    }

    auto exit_path =
        plan_in_animation(TP, p.second, start_pose,
                          home_poses.at(robot), p.second + 5, robot, true);
    exit_path.r = robot;
    exit_path.task_index = task_index;
    exit_path.is_exit = true;
    exit_path.name = "exit";

    /*for (uint i=0; i<exit_path.t.N; ++i){
      std::cout << exit_path.path[i] << std::endl;
      CPlanner.setJointState(exit_path.path[i]);
      CPlanner.watch(true);
    }*/

    if (exit_path.has_solution) {
      spdlog::info("Adding exit path for robot {} at time {}", robot.prefix, p.second);

      const auto exit_anim_part = make_animation_part(
          CPlanner, exit_path.path, robot_frames[robot], p.second);
      exit_path.anim = exit_anim_part;
      paths[robot].push_back(exit_path);
    } else {
      spdlog::info("Was not able to find an exit path");
      return PlanResult(PlanStatus::failed);
    }
    /*{
      rai::Animation An;
      for (const auto &p2 : paths) {
        for (const auto path2 : p2.second) {
          An.A.append(path2.anim);
        }
      }
      An.play(CPlanner, false);
    }*/
  }

  PrioritizedTaskPlanner planner(home_poses, rtpm, best_makespan_so_far, early_stopping);
  
  // actually plan
  for (uint i = start_index; i < sequence.size(); ++i) {
    uint prev_finishing_time = 0;

    for (const auto &p : paths) {
      const auto plans = p.second;
      if (plans.size() > 0) {
        if (plans.back().is_exit) {
          // if a path is an exit, we are looking at the maximum non-exit time
          if (plans.size() > 1){
            prev_finishing_time = std::max(
                {uint(plans[plans.size() - 2].t(-1)), prev_finishing_time});
          }
          else{ // the first path can be an exit path that leads from the start pose to the home pose
            prev_finishing_time = 0;
          }
        } else {
          // else, we are looking at the final current time
          prev_finishing_time = std::max(
              {uint(plans[plans.size() - 1].t(-1)), prev_finishing_time});
        }
      }
    }

    spdlog::info("Planning for obj {}", sequence[i].task.object);
    const auto res = planner.plan(CPlanner, sequence[i], prev_finishing_time, paths);
    // const auto res = plan_task(CPlanner, sequence[i], rtpm,
    //                            best_makespan_so_far, home_poses,
    //                            prev_finishing_time, early_stopping, paths);

    // TODO: fix this crap
    // const auto obj = STRING("obj" << sequence[i].task.object + 1);
    // auto to = CPlanner[obj];
    // auto from = CPlanner["table_base"];

    // to->unLink();

    // // create a new joint
    // to->linkFrom(from, true);

    if (res != PlanStatus::success) {
      spdlog::info("Failed planning");
      return PlanResult(res);
    }

    // TODO: compute better estimate for early stopping
    const uint current_makespan = get_makespan_from_plan(paths);
    const uint lb_remaining_makespan = 0;
    const uint estimated_makespan = current_makespan + lb_remaining_makespan;

    if (early_stopping && estimated_makespan > best_makespan_so_far) {
      return PlanResult(PlanStatus::aborted);
    }
  }

  if (false) {
    rai::Animation A = make_animation_from_plan(paths);

    for (uint i = 0; i < A.getT(); ++i) {
      A.setToTime(CPlanner, i);
      CPlanner.watch(false);
      rai::wait(0.1);
    }
  }

  return PlanResult(PlanStatus::success, paths);
}

// overload (not in the literal or in the c++ sense) of the above
PlanResult plan_multiple_arms_given_sequence(
    rai::Configuration C, const RobotTaskPoseMap &rtpm,
    const OrderedTaskSequence &sequence, const std::unordered_map<Robot, arr> &home_poses,
    const uint best_makespan_so_far = 1e6, const bool early_stopping = false) {

  Plan paths;
  return plan_multiple_arms_given_subsequence_and_prev_plan(
      C, rtpm, sequence, 0, paths, home_poses, best_makespan_so_far,
      early_stopping);
}