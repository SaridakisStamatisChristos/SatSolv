# ADR-0001: C++20 for the SatSolv core

Status: accepted.

SatSolv uses C++20 because CDCL performance depends on compact literal/clause storage, predictable memory access, and low-overhead watched-literal propagation. The project confines ownership to standard containers and integer clause IDs; learnt clauses are never physically freed while a solve is active, which prevents dangling reason references. Python is used only for deliberately independent verification and benchmark orchestration.
