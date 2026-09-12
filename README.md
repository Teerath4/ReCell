# ReCell
A pulse-test diagnostic node that estimates second-life EV battery health in minutes using edge AI on Arduino UNO Q — no expensive lab equipment required.

Second-life battery triage is a real, active research area, and I'm not claiming to have invented pulse-based SoH estimation or ML-based battery diagnostics — both are well-established techniques in published research. What I set out to build was a specific, integrated system: real sensing hardware, a trained model, and a working dashboard, built from the ground up rather than relying on a pre-built BMS chip, so that every part of the pipeline - from raw voltage to a technician-facing recommendation  is something I designed, built, and debugged myself.
