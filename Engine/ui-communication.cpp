#include "engine-search.h"
#include <sstream>
#include <iostream>

void InitialiseAll(){
    engine.transposition_table_size_MB = engine_transposition_table_size_MB;
    TT.SetSize(engine.transposition_table_size_MB);
    InitialiseZobristKeys();
    InitialiseHashKey();
    InitialiseLMRTables();
    PrecomputePawnAttacksTable();
    PrecomputeKnightAttacksTable();
    PrecomputeBishopAttacksTable();
    PrecomputeRookAttacksTable();
    PrecomputeKingAttacksTable();
    game.steady_ply = 0;
    game.history_stack[game.steady_ply].hash_key = board.hash_key;
}

// Returns 0 is the move is illegal
uint16_t ConvertLANMoveToEngineMoveIfLegal(std::string LAN){
    uint16_t selected_move = 0;

    // Extract source, target and promotion type
    int source_file = LAN[0] - 'a';
    int source_rank = 8 - (LAN[1] - '0');
    int source = (8 * source_rank) + source_file;

    int target_file = LAN[2] - 'a';
    int target_rank = 8 - (LAN[3] - '0');
    int target = (8 * target_rank) + target_file;

    Piece promotion_type;
    switch(LAN[4]){
        case 'n': { promotion_type = Piece::knight; break; }
        case 'b': { promotion_type = Piece::bishop; break; }
        case 'r': { promotion_type = Piece::rook; break; }
        case 'q': { promotion_type = Piece::queen; break; }
        default: { promotion_type = Piece::NO_PIECE; break; }
    }

    MoveList list; GeneratePseudoLegalMoves(list);
    uint16_t move;
    for(int i = 0; i < list.count; i++){
        move = list.list[i];
        int move_source = move & 0b0000000000111111;
        int move_target = (move & 0b0000111111000000) >> 6;
        int move_flag = (move & 0b1111000000000000) >> 12;

        if(source == move_source && target == move_target){
            if(promotion_type == Piece::NO_PIECE && move_flag < 8){
                selected_move = move;
                break;
            }

            else{
                if(
                    promotion_type == Piece::knight &&
                    (move_flag == MoveFlag::quiet_knight_promo || move_flag == MoveFlag::capture_knight_promo)
                ){
                    selected_move = move; break;
                }

                else if(
                    promotion_type == Piece::bishop &&
                    (move_flag == MoveFlag::quiet_bishop_promo || move_flag == MoveFlag::capture_bishop_promo)
                ){
                    selected_move = move; break;
                }

                else if(
                    promotion_type == Piece::rook &&
                    (move_flag == MoveFlag::quiet_rook_promo || move_flag == MoveFlag::capture_rook_promo)
                ){
                    selected_move = move; break;
                }

                else if(
                    promotion_type == Piece::queen &&
                    (move_flag == MoveFlag::quiet_queen_promo || move_flag == MoveFlag::capture_queen_promo)
                ){
                    selected_move = move; break;
                }
            }
        }
    }

    // Full legality check
    if(selected_move){
        UnmakeMoveGameState irr_info = board.MakeMove(selected_move, board.to_move);
        if(board.InCheck(static_cast<Colour>(!board.to_move))){
            board.UnmakeMove(selected_move, board.to_move, irr_info);
            selected_move = 0;
        } else{
            board.UnmakeMove(selected_move, board.to_move, irr_info);
        }
    }

    return selected_move;
}

// Game end status for the side whose turn it is to move
enum GameEnd { in_play, checkmate, stalemate };
GameEnd GameFinishedType(){
    MoveList list; GeneratePseudoLegalMoves(list);
    int legal_moves = 0;
    for(int i = 0; i < list.count; i++){
        UnmakeMoveGameState irr_info = board.MakeMove(list.list[i], board.to_move);
        if(board.InCheck(static_cast<Colour>(!board.to_move))){
            board.UnmakeMove(list.list[i], board.to_move, irr_info);
            continue;
        } else{
            board.UnmakeMove(list.list[i], board.to_move, irr_info);
            legal_moves++;
        }
    }

    if(legal_moves){ return GameEnd::in_play; }

    else{
        return (board.InCheck(board.to_move) ? GameEnd::checkmate : GameEnd::stalemate);
    }
}

struct GoParameters {
    int wtime = -1;
    int btime = -1;
    int winc = 0;
    int binc = 0;
    int movestogo = -1;
    int movetime = -1;
    int depth = -1;
    int nodes = -1;
    bool infinite = false;
    bool ponder = false;
    std::vector<std::string> searchmoves;
};

void ParseUCICommand_ucinewgame(){
    memset(PV_table, 0, sizeof(PV_table));
    memset(last_PV_table, 0, sizeof(last_PV_table));
    memset(PV_length, 0, sizeof(PV_length));
    memset(last_PV_length, 0, sizeof(last_PV_length));
    memset(history_moves, 0, sizeof(history_moves));
    WipeKillerTable();
    nodes_searched = 0;
    TT.Wipe();
}

void ParseUCICommand_position(std::stringstream& strstr){
    std::string token;
    strstr >> token;

    if(token == "startpos"){
        ParseFEN(start_pos);
    } else if(token == "fen"){
        std::string full_fen = "";
        std::string fen_part = "";
        for(int i = 0; i < 6; i++){
            if(strstr >> fen_part){
                full_fen += (i == 0 ? "" : " ") + fen_part;
            }
        }

        ParseFEN(full_fen);
    }



    if(strstr >> token && token == "moves"){
        std::string move_str;
        uint16_t move;
        while(strstr >> move_str){
            move = ConvertLANMoveToEngineMoveIfLegal(move_str);
            board.MakeMove(move, board.to_move);
        }
    }
}

int ComputeMoveTimeMs(GoParameters params){
    int move_time_ms = 0;
    
    if(params.movetime != -1) {
        return params.movetime;
    }

    int remaining_time = (board.to_move == Colour::white) ? params.wtime : params.btime;
    int increment = (board.to_move == Colour::white) ? params.winc : params.binc;

    if(remaining_time <= 0){ return 100; } // Fallback safety margin

    // Fallback for moves left
    int moves_left = (params.movestogo > 0) ? params.movestogo : 30;

    int target_time = (remaining_time / moves_left) + increment - 50; // Safety buffer - 50ms
    
    return std::max(target_time, 10); // Always give at least 10ms
}

void ParseUCICommand_go(std::stringstream& strstr){
    std::string token;
    GoParameters params;

    while(strstr >> token){
        if (token == "wtime"){ strstr >> params.wtime; }
        else if (token == "btime"){ strstr >> params.btime; }
        else if (token == "winc"){ strstr >> params.winc; }
        else if (token == "binc"){ strstr >> params.binc; }
        else if (token == "movestogo"){ strstr >> params.movestogo; }
        else if (token == "movetime"){ strstr >> params.movetime; }
        else if (token == "depth"){ strstr >> params.depth; }
        else if (token == "nodes"){ strstr >> params.nodes; }
        else if (token == "infinite"){ params.infinite = true; }
        else if (token == "ponder"){ params.ponder = true; }
    }

    // Compute move time --------------

    int move_time_ms = params.infinite ? -1 : ComputeMoveTimeMs(params);

    std::cout << move_time_ms << "\n";

    // --------------------------------

    search_timer.CancelTimer();

    if(params.infinite){
        std::thread search_thread([params](){
            params.depth == -1 ? engine.IterativeSearch(MAX_PLY) : engine.IterativeSearch(params.depth);
        });
        search_thread.detach();
    } else{
        std::thread search_thread([params, move_time_ms](){
            if(params.depth == -1){
                search_timer.SetTimer(move_time_ms);
                engine.IterativeSearch(MAX_PLY);
                search_timer.CancelTimer();
            }

            else{
                engine.IterativeSearch(params.depth);
            }
        });
        search_thread.detach();
    }
}

void ParseUCICommand_stop(std::stringstream& strstr){
    search_timer.StopImmediately();
}

void UCILoop(){
    std::string line;

    while(std::getline(std::cin, line)){
        if(!line.empty() && line.back() == '\r'){ line.pop_back(); }

        std::stringstream strstr(line);
        std::string command;
        strstr >> command;

        if(command == "uci"){
            std::cout << "id name Xess 0.0\n";
            std::cout << "id author fept\n";
            std::cout << "uciok" << std::endl;
        }

        else if(command == "isready"){
            std::cout << "readyok";
        }

        else if(command == "ucinewgame"){
            ParseUCICommand_ucinewgame();
        }

        else if(command == "position"){
            ParseUCICommand_position(strstr);
        }

        else if(command == "go"){
            ParseUCICommand_go(strstr);
        }

        else if(command == "stop"){
            ParseUCICommand_stop(strstr);
        }

        else if(command == "quit"){
            break;
        }
    }
}

int main(){

    // UPDATE THE HISTORY STACK AND STEADY PLY EVERYWHERE THEY NEED TO BE IN THE UCI SYSTEM
    // POSSIBLY WIPE THE TABLES AT THE END OF IterativeSearch()

    InitialiseAll();

    //std::stringstream x("fen r1b1r1k1/1ppq1ppp/1nn5/pQ2p1B1/3b4/2NP1NP1/PP2PPBP/R1R3K1 w - - 4 13");
    //ParseUCICommand_position(x);

    //std::stringstream y("go wtime 165400 btime 172100 winc 2000 binc 2000 movestogo 36");
    //ParseUCICommand_go(y);

    //std::stringstream z("go infinite");
    //ParseUCICommand_go(z);

    UCILoop();

    ParseFEN(FEN);
    InitialiseAll();
    PrintBoardToTerminal();

    // ------------

    // Gameplay loop
    while(GameFinishedType() == GameEnd::in_play){

        // Player to move
        if(board.to_move == player_playing_as){
            std::string player_input; uint16_t player_move; bool legal_move_chosen = false;
            while(!legal_move_chosen){
                std::cout  << "Enter move: "; std::cin >> player_input;
                player_move = ConvertLANMoveToEngineMoveIfLegal(player_input);
                if(player_move){ legal_move_chosen = true; }
            }

            board.MakeMove(player_move, board.to_move);
            game.steady_ply++;
            game.history_stack[game.steady_ply].hash_key = board.hash_key;
        }

        // Engine to move
        else{
            //std::thread th(StartSearchTimer, 15000);
            //engine.IterativeSearch();
            // NEED TO TERMINATE THE FUNCTION, NOT JUST DELETE THE THREAD
            //th.detach(); th.~thread();
            //stop = false;

            board.MakeMove(last_PV_table[0][0], board.to_move);
            game.steady_ply++;
            game.history_stack[game.steady_ply].hash_key = board.hash_key;

            engine.ClearTablesResetNodeCount();
        }

        PrintBoardToTerminal();
    }

    return 0;
}