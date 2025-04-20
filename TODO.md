# TODO — Genetic Art GA (C99 + SDL2)

This document lists ongoing and planned tasks for the project.

---

##  Done

- [x] Basic GA implementation with mutation, crossover, and elitism
- [x] Circle and triangle rasterizers with alpha blending
- [x] Visual feedback: side-by-side display (reference vs candidate)
- [x] Rework data structures (GAParams, Gene, Chromosome)
- [x] Multithreading: fitness evaluation using pthreads and barriers
- [x] Fix Ctrl+C deadlock by enforcing symmetrical barrier usage
- [x] Ensure every commit builds and runs cleanly (at least on Linux)

---

##  In Progress

- [ ] Tune GA parameters (population size, mutation rate, etc.)
- [ ] Document project in a clear and research-friendly way (README / description)
- [ ] Structure commits to reflect thinking process and iterations
- [ ] Add TODOs in code (short-term ideas, optimizations, etc.)

---

##  Next Steps: Smarter GA

- [ ] Upgrade GA logic with smarter mechanisms:
  - [ ] Adaptive mutation rate
  - [ ] Fitness-based parent selection (e.g., tournament or roulette)
  - [ ] Better crossover strategies (e.g., uniform crossover)
  - [ ] Gene-level variation based on prior impact (heuristic feedback)
- [ ] Possibly integrate constraint-based evolution (e.g., target edge detection)

---

##  Performance & Hardware

- [ ] Profile main bottlenecks (render, fitness, memory usage)
- [ ] Optionally implement AVX512 or SIMD fallback tuning
- [ ] Add startup system check to configure internal states for best performance
- [ ] Consider GPU acceleration later (only after smarter logic is in place)

---

##  R&D / Experiments

- [ ] Support other shape types (e.g., rectangles, bezier curves)
- [ ] Add headless mode for benchmarking
- [ ] Allow dynamic adjustment of GA parameters during runtime
- [ ] Compare convergence speed with and without adaptive strategies
- [ ] Try evolving image sequences or animations (experimental)

---

##  Misc / Long-Term

- [ ] Clean code formatting and consistent comments
- [ ] Add test image suite with different levels of difficulty
- [ ] Make codebase easy to fork / extend for other domains
- [ ] Consider saving/restoring GA state (serialization)

---

> Thinking in progress...
