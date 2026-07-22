# conftest.py for moba-sim Python tests.
#
# nanobind types don't participate in Python's cyclic GC.  When a Simulation's
# signal callback captures the Simulation itself (common in tests), a reference
# cycle is created that Python's GC cannot collect.  This is harmless during
# normal operation (the cycle is broken when the C++ destructor runs), but
# produces "leaked instance" warnings at interpreter shutdown.
#
# To suppress these warnings in tests, call sim.clear_signals() before
# discarding a Simulation that has signal subscribers.