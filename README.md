# Xess

## Overview
Xess is a lightweight UCI bitboard chess engine written in C++. **Always** compile with '-O3' for best performance.

## Engine Search
Xess uses magic bitboards for fast move generation and filtering legal moves. It searches with a traditional alpha-beta algorithm that revolves around principal variation search (PVS). This is highly optimised by a selection of move ordering heuristics and several pruning heuristics.

## Evaluation
The current evaluation is a simple hand-crafted tapered evaluation that scores material with piece-value tables, evaluates pawn structure, and deploys a full understanding of insufficient material when necessary. There exist plans for a **NNUE** evaluation in the future.