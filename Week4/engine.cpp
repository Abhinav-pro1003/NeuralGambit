// =============================================================================
// engine.cpp  —  Week-4 UCI Chess Engine  (final version)
//
// Features
//   • Self-contained move generator: en passant, castling, promotions
//     (no external chess library — board representation, attack detection,
//      and make/unmake are all implemented from scratch)
//   • UCI protocol (uci / isready / ucinewgame / position / go / stop / quit)
//   • Negamax alpha-beta search with iterative deepening, time-based
//     termination that always falls back to the best move from the last
//     *completed* depth if a deeper search is interrupted
//   • Zobrist hashing, built directly into the Board struct and maintained
//     incrementally on every make/unmake
//   • Transposition table (64 MB, depth-preferred replacement) keyed on the
//     board's own Zobrist hash
//   • Quiescence search (captures + promotions only) to avoid the horizon
//     effect at leaf nodes
//   • Move ordering: TT move first → MVV-LVA captures → killer moves →
//     history heuristic for remaining quiet moves
//   • Check extension (+1 ply when the side to move is in check)
//   • Material + piece-square-table evaluation
//   • Multithreaded UCI loop: search runs on a background thread so "stop"
//     is handled immediately instead of blocking the main input loop
//
// Build
//   g++ -std=c++17 -O2 -pthread -o engine engine.cpp
//   (or: make / cmake — see accompanying Makefile / CMakeLists.txt)
//
// Usage (manual UCI session or plug into CuteChess / Arena / any UCI GUI)
//   ./engine
//   uci          → engine replies id/uciok
//   isready      → readyok
//   position startpos moves e2e4 e7e5
//   go movetime 5000
//   bestmove e2e4
// =============================================================================

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <climits>
#include <atomic>
#include <thread>
#include <random>
#include <cstdint>
#include <cmath>

// =============================================================================
// SQUARE HELPERS
// =============================================================================
// Square layout: a1=0, b1=1, …, h1=7, a2=8, …, h8=63

inline int file_of(int sq) { return sq & 7; }
inline int rank_of(int sq) { return sq >> 3; }
inline int make_sq(int f, int r) { return (r << 3) | f; }
inline int mirror_sq(int sq) { return sq ^ 56; }   // flip rank (white<->black)

// =============================================================================
// PIECE ENCODING
// =============================================================================
// bit-layout:  bit-3 = color (0=white 1=black),  bits 0-2 = type
// EMPTY = 0
// White: WP=1 WN=2 WB=3 WR=4 WQ=5 WK=6
// Black: BP=9 BN=10 BB=11 BR=12 BQ=13 BK=14  (type = p & 7)

static const int WHITE=0, BLACK=1;
static const int PAWN=1, KNIGHT=2, BISHOP=3, ROOK=4, QUEEN=5, KING=6;
static const int EMPTY=0;
static const int WP=1,WN=2,WB=3,WR=4,WQ=5,WK=6;
static const int BP=9,BN=10,BB=11,BR=12,BQ=13,BK=14;

inline int make_piece(int color, int type) { return (color << 3) | type; }
inline int piece_type(int p)   { return p & 7; }
inline int piece_color(int p)  { return p >> 3; }

// Castling rights bitmask
static const int CASTLE_WK=1, CASTLE_WQ=2, CASTLE_BK=4, CASTLE_BQ=8;

// Material values (centipawns)
static const int PIECE_VALUE[7] = { 0, 100, 320, 330, 500, 900, 20000 };

// =============================================================================
// PIECE-SQUARE TABLES  (simplified evaluation by Tomasz Michniewski / CPW)
// Index [piece_type][square].  square 0=a1 … 63=h8 (rank-1 first).
// For BLACK use mirror_sq() on the square index.
// =============================================================================
static const int PST[7][64] = {
// [0] NONE
{0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0},
// [1] PAWN
{  0,  0,  0,  0,  0,  0,  0,  0,   // rank 1
   5, 10, 10,-20,-20, 10, 10,  5,   // rank 2
   5, -5,-10,  0,  0,-10, -5,  5,   // rank 3
   0,  0,  0, 20, 20,  0,  0,  0,   // rank 4
   5,  5, 10, 25, 25, 10,  5,  5,   // rank 5
  10, 10, 20, 30, 30, 20, 10, 10,   // rank 6
  50, 50, 50, 50, 50, 50, 50, 50,   // rank 7
   0,  0,  0,  0,  0,  0,  0,  0},  // rank 8
// [2] KNIGHT
{-50,-40,-30,-30,-30,-30,-40,-50,
 -40,-20,  0,  5,  5,  0,-20,-40,
 -30,  5, 10, 15, 15, 10,  5,-30,
 -30,  0, 15, 20, 20, 15,  0,-30,
 -30,  5, 15, 20, 20, 15,  5,-30,
 -30,  0, 10, 15, 15, 10,  0,-30,
 -40,-20,  0,  0,  0,  0,-20,-40,
 -50,-40,-30,-30,-30,-30,-40,-50},
// [3] BISHOP
{-20,-10,-10,-10,-10,-10,-10,-20,
 -10,  5,  0,  0,  0,  0,  5,-10,
 -10, 10, 10, 10, 10, 10, 10,-10,
 -10,  0, 10, 10, 10, 10,  0,-10,
 -10,  5,  5, 10, 10,  5,  5,-10,
 -10,  0,  5, 10, 10,  5,  0,-10,
 -10,  0,  0,  0,  0,  0,  0,-10,
 -20,-10,-10,-10,-10,-10,-10,-20},
// [4] ROOK
{  0,  0,  0,  5,  5,  0,  0,  0,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
   5, 10, 10, 10, 10, 10, 10,  5,
   0,  0,  0,  0,  0,  0,  0,  0},
// [5] QUEEN
{-20,-10,-10, -5, -5,-10,-10,-20,
 -10,  0,  5,  0,  0,  0,  0,-10,
 -10,  5,  5,  5,  5,  5,  0,-10,
   0,  0,  5,  5,  5,  5,  0, -5,
  -5,  0,  5,  5,  5,  5,  0, -5,
 -10,  0,  5,  5,  5,  5,  0,-10,
 -10,  0,  0,  0,  0,  0,  0,-10,
 -20,-10,-10, -5, -5,-10,-10,-20},
// [6] KING  (middlegame — castle and hide)
{ 20, 30, 10,  0,  0, 10, 30, 20,
  20, 20,  0,  0,  0,  0, 20, 20,
 -10,-20,-20,-20,-20,-20,-20,-10,
 -20,-30,-30,-40,-40,-30,-30,-20,
 -30,-40,-40,-50,-50,-40,-40,-30,
 -30,-40,-40,-50,-50,-40,-40,-30,
 -30,-40,-40,-50,-50,-40,-40,-30,
 -30,-40,-40,-50,-50,-40,-40,-30}
};

// =============================================================================
// ZOBRIST HASHING  (built directly into the Board struct — see below)
// =============================================================================
static uint64_t ZOB_PIECE[15][64];
static uint64_t ZOB_EP[64];
static uint64_t ZOB_CASTLE[16];
static uint64_t ZOB_SIDE;

void init_zobrist() {
    std::mt19937_64 rng(0xDEADBEEFCAFEBABEULL);
    for (int p = 0; p < 15; ++p)
        for (int sq = 0; sq < 64; ++sq)
            ZOB_PIECE[p][sq] = rng();
    for (int sq = 0; sq < 64; ++sq) ZOB_EP[sq]   = rng();
    for (int i  = 0; i  < 16; ++i)  ZOB_CASTLE[i] = rng();
    ZOB_SIDE = rng();
}

// =============================================================================
// MOVE
// =============================================================================
static const int FLAG_EP     = 1;   // en-passant capture
static const int FLAG_CASTLE = 2;   // castling

struct Move {
    uint8_t from  = 0;
    uint8_t to    = 0;
    uint8_t promo = 0;   // 0=none, or KNIGHT..QUEEN
    uint8_t flags = 0;   // FLAG_EP / FLAG_CASTLE

    bool is_none() const { return from == 0 && to == 0 && promo == 0 && flags == 0; }
    bool operator==(const Move& o) const {
        return from==o.from && to==o.to && promo==o.promo && flags==o.flags;
    }
    bool operator!=(const Move& o) const { return !(*this == o); }
};

// Castling mask: AND this with castling rights when a piece moves from/to that square
// Values derived from: 15 & ~(rights that square is responsible for)
// a1=~WQ=13, e1=~(WK|WQ)=12, h1=~WK=14
// a8=~BQ=7,  e8=~(BK|BQ)=3,  h8=~BK=11
static const int CASTLE_MASK[64] = {
    13,15,15,15,12,15,15,14,  // rank 1
    15,15,15,15,15,15,15,15,  // rank 2
    15,15,15,15,15,15,15,15,  // rank 3
    15,15,15,15,15,15,15,15,  // rank 4
    15,15,15,15,15,15,15,15,  // rank 5
    15,15,15,15,15,15,15,15,  // rank 6
    15,15,15,15,15,15,15,15,  // rank 7
     7,15,15,15, 3,15,15,11   // rank 8
};

// =============================================================================
// BOARD
// =============================================================================
static const int MAX_PLY = 512;

struct Undo {
    int      ep, castling, halfmove, captured;
    uint64_t hash;
};

struct Board {
    int      sq[64];
    int      side;
    int      ep;         // en-passant target square, -1 = none
    int      castling;   // 4-bit mask
    int      halfmove;
    int      fullmove;
    uint64_t hash;        // <-- the board's own, incrementally-maintained Zobrist hash
    int      king_sq[2]; // position of each king

    Undo     hist[MAX_PLY];
    int      ply;

    void reset() {
        memset(sq, 0, sizeof(sq));
        side=WHITE; ep=-1; castling=0;
        halfmove=0; fullmove=1; hash=0; ply=0;
        king_sq[WHITE]=king_sq[BLACK]=-1;
    }
};

// =============================================================================
// FEN  PARSING
// =============================================================================
static int piece_from_char(char c) {
    switch(c) {
        case 'P': return WP; case 'N': return WN; case 'B': return WB;
        case 'R': return WR; case 'Q': return WQ; case 'K': return WK;
        case 'p': return BP; case 'n': return BN; case 'b': return BB;
        case 'r': return BR; case 'q': return BQ; case 'k': return BK;
    }
    return EMPTY;
}

void load_fen(Board& b, const std::string& fen) {
    b.reset();
    std::istringstream ss(fen);
    std::string pos, side_s, cast_s, ep_s;
    int half=0, full=1;
    ss >> pos >> side_s >> cast_s >> ep_s;
    if (!(ss >> half)) half = 0;
    if (!(ss >> full)) full = 1;

    // piece placement (FEN goes rank-8 … rank-1)
    int r=7, f=0;
    for (char c : pos) {
        if      (c == '/') { --r; f=0; }
        else if (c >= '1' && c <= '8') { f += c-'0'; }
        else {
            int s = r*8+f, p = piece_from_char(c);
            b.sq[s] = p;
            if (piece_type(p) == KING) b.king_sq[piece_color(p)] = s;
            ++f;
        }
    }

    b.side     = (side_s == "b") ? BLACK : WHITE;
    b.halfmove = half;
    b.fullmove = full;

    b.castling = 0;
    if (cast_s != "-")
        for (char c : cast_s) {
            if      (c=='K') b.castling |= CASTLE_WK;
            else if (c=='Q') b.castling |= CASTLE_WQ;
            else if (c=='k') b.castling |= CASTLE_BK;
            else if (c=='q') b.castling |= CASTLE_BQ;
        }

    b.ep = -1;
    if (ep_s != "-" && ep_s.size() >= 2)
        b.ep = (ep_s[1]-'1')*8 + (ep_s[0]-'a');

    // Compute Zobrist hash from scratch once (then maintained incrementally)
    b.hash = 0;
    for (int sq=0; sq<64; ++sq)
        if (b.sq[sq]) b.hash ^= ZOB_PIECE[b.sq[sq]][sq];
    if (b.ep   >= 0) b.hash ^= ZOB_EP[b.ep];
    b.hash ^= ZOB_CASTLE[b.castling];
    if (b.side == BLACK) b.hash ^= ZOB_SIDE;
}

// =============================================================================
// ATTACK  DETECTION
// =============================================================================
bool is_attacked(const Board& b, int sq, int by_color) {
    int r = rank_of(sq), f = file_of(sq);

    // Pawns
    if (by_color == WHITE) {
        if (r>0 && f>0 && b.sq[sq-9]==WP) return true;
        if (r>0 && f<7 && b.sq[sq-7]==WP) return true;
    } else {
        if (r<7 && f>0 && b.sq[sq+7]==BP) return true;
        if (r<7 && f<7 && b.sq[sq+9]==BP) return true;
    }

    // Knights
    static const int KN[8][2]={{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
    for (auto& d : KN) {
        int nf=f+d[0], nr=r+d[1];
        if (nf<0||nf>7||nr<0||nr>7) continue;
        int p=b.sq[nr*8+nf];
        if (p && piece_color(p)==by_color && piece_type(p)==KNIGHT) return true;
    }

    // Diagonals: bishop / queen
    static const int DD[4][2]={{1,1},{1,-1},{-1,1},{-1,-1}};
    for (auto& d : DD) {
        int cf=f+d[0], cr=r+d[1];
        while (cf>=0&&cf<=7&&cr>=0&&cr<=7) {
            int p=b.sq[cr*8+cf];
            if (p) {
                if (piece_color(p)==by_color &&
                   (piece_type(p)==BISHOP || piece_type(p)==QUEEN)) return true;
                break;
            }
            cf+=d[0]; cr+=d[1];
        }
    }

    // Orthogonals: rook / queen
    static const int OD[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
    for (auto& d : OD) {
        int cf=f+d[0], cr=r+d[1];
        while (cf>=0&&cf<=7&&cr>=0&&cr<=7) {
            int p=b.sq[cr*8+cf];
            if (p) {
                if (piece_color(p)==by_color &&
                   (piece_type(p)==ROOK || piece_type(p)==QUEEN)) return true;
                break;
            }
            cf+=d[0]; cr+=d[1];
        }
    }

    // King
    static const int KD[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    for (auto& d : KD) {
        int nf=f+d[0], nr=r+d[1];
        if (nf<0||nf>7||nr<0||nr>7) continue;
        int p=b.sq[nr*8+nf];
        if (p && piece_color(p)==by_color && piece_type(p)==KING) return true;
    }
    return false;
}

inline bool in_check(const Board& b, int side) {
    return b.king_sq[side]>=0 && is_attacked(b, b.king_sq[side], 1-side);
}

// =============================================================================
// MOVE  GENERATION
// =============================================================================
struct MoveList {
    Move moves[256];
    int  cnt = 0;

    void push(int from, int to, int promo=0, int flags=0) {
        moves[cnt++] = {(uint8_t)from,(uint8_t)to,(uint8_t)promo,(uint8_t)flags};
    }
    void push_promos(int from, int to, int flags=0) {
        push(from,to,KNIGHT,flags); push(from,to,BISHOP,flags);
        push(from,to,ROOK,  flags); push(from,to,QUEEN, flags);
    }
};

// gen_moves: pseudo-legal.
//   captures_only = true → only captures + promotions (for quiescence search)
void gen_moves(const Board& b, MoveList& ml, bool captures_only=false) {
    int us=b.side, them=1-us;

    for (int sq=0; sq<64; ++sq) {
        int p=b.sq[sq];
        if (!p || piece_color(p)!=us) continue;
        int type=piece_type(p);
        int r=rank_of(sq), f=file_of(sq);

        // ---- PAWN ----
        if (type==PAWN) {
            int dir     = (us==WHITE) ?  8 : -8;
            int start_r = (us==WHITE) ?  1 :  6;   // rank for double push
            int promo_r = (us==WHITE) ?  6 :  1;   // rank that triggers promotion

            int fwd = sq+dir;
            if (fwd>=0 && fwd<64 && !b.sq[fwd]) {
                if (r==promo_r) {
                    // Promotions are always generated (even in captures_only mode)
                    ml.push_promos(sq, fwd);
                } else if (!captures_only) {
                    ml.push(sq, fwd);
                    if (r==start_r && !b.sq[fwd+dir])
                        ml.push(sq, fwd+dir);
                }
            }

            // Diagonal captures + en passant
            for (int df : {-1, 1}) {
                int cf = f+df;
                if (cf<0||cf>7) continue;
                int cap_r = r + (us==WHITE ? 1 : -1);
                if (cap_r<0||cap_r>7) continue;
                int to = cap_r*8+cf;
                bool normal_cap = b.sq[to] && piece_color(b.sq[to])==them;
                bool ep_cap     = (to==b.ep && b.ep>=0);
                if (normal_cap || ep_cap) {
                    int fl = ep_cap ? FLAG_EP : 0;
                    if (r==promo_r) ml.push_promos(sq, to, fl);
                    else            ml.push(sq, to, 0, fl);
                }
            }
            continue;
        }

        // ---- KNIGHT ----
        if (type==KNIGHT) {
            static const int NJ[8][2]={{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
            for (auto& d : NJ) {
                int nf=f+d[0], nr=r+d[1];
                if (nf<0||nf>7||nr<0||nr>7) continue;
                int to=nr*8+nf, tp=b.sq[to];
                if (!tp || piece_color(tp)==them)
                    if (!captures_only || tp)
                        ml.push(sq, to);
            }
            continue;
        }

        // ---- BISHOP / QUEEN (diagonals) ----
        if (type==BISHOP || type==QUEEN) {
            static const int DD[4][2]={{1,1},{1,-1},{-1,1},{-1,-1}};
            for (auto& d : DD) {
                int cf=f+d[0], cr=r+d[1];
                while (cf>=0&&cf<=7&&cr>=0&&cr<=7) {
                    int to=cr*8+cf, tp=b.sq[to];
                    if (tp) { if (piece_color(tp)==them) ml.push(sq,to); break; }
                    if (!captures_only) ml.push(sq, to);
                    cf+=d[0]; cr+=d[1];
                }
            }
        }

        // ---- ROOK / QUEEN (orthogonals) ----
        if (type==ROOK || type==QUEEN) {
            static const int OD[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
            for (auto& d : OD) {
                int cf=f+d[0], cr=r+d[1];
                while (cf>=0&&cf<=7&&cr>=0&&cr<=7) {
                    int to=cr*8+cf, tp=b.sq[to];
                    if (tp) { if (piece_color(tp)==them) ml.push(sq,to); break; }
                    if (!captures_only) ml.push(sq, to);
                    cf+=d[0]; cr+=d[1];
                }
            }
        }

        // ---- KING ----
        if (type==KING) {
            static const int KD[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
            for (auto& d : KD) {
                int nf=f+d[0], nr=r+d[1];
                if (nf<0||nf>7||nr<0||nr>7) continue;
                int to=nr*8+nf, tp=b.sq[to];
                if (!tp || piece_color(tp)==them)
                    if (!captures_only || tp)
                        ml.push(sq, to);
            }
            // Castling (never in captures_only mode)
            if (!captures_only) {
                if (us==WHITE) {
                    if ((b.castling&CASTLE_WK) && !b.sq[5]&&!b.sq[6] &&
                        !is_attacked(b,4,BLACK)&&!is_attacked(b,5,BLACK)&&!is_attacked(b,6,BLACK))
                        ml.push(4, 6, 0, FLAG_CASTLE);
                    if ((b.castling&CASTLE_WQ) && !b.sq[3]&&!b.sq[2]&&!b.sq[1] &&
                        !is_attacked(b,4,BLACK)&&!is_attacked(b,3,BLACK)&&!is_attacked(b,2,BLACK))
                        ml.push(4, 2, 0, FLAG_CASTLE);
                } else {
                    if ((b.castling&CASTLE_BK) && !b.sq[61]&&!b.sq[62] &&
                        !is_attacked(b,60,WHITE)&&!is_attacked(b,61,WHITE)&&!is_attacked(b,62,WHITE))
                        ml.push(60, 62, 0, FLAG_CASTLE);
                    if ((b.castling&CASTLE_BQ) && !b.sq[59]&&!b.sq[58]&&!b.sq[57] &&
                        !is_attacked(b,60,WHITE)&&!is_attacked(b,59,WHITE)&&!is_attacked(b,58,WHITE))
                        ml.push(60, 58, 0, FLAG_CASTLE);
                }
            }
        }
    }
}

// =============================================================================
// MAKE / UNMAKE  MOVE
// =============================================================================
void make_move(Board& b, const Move& m) {
    int from=m.from, to=m.to;
    int us=b.side, them=1-us;
    int p=b.sq[from];
    int type=piece_type(p);
    int captured=b.sq[to];

    // Save undo information
    Undo& u = b.hist[b.ply++];
    u = { b.ep, b.castling, b.halfmove, captured, b.hash };

    // Incrementally update hash: remove old castling & ep contributions
    b.hash ^= ZOB_CASTLE[b.castling];
    if (b.ep>=0) b.hash ^= ZOB_EP[b.ep];

    // Move the piece
    b.hash ^= ZOB_PIECE[p][from];
    b.sq[from] = EMPTY;
    if (captured) b.hash ^= ZOB_PIECE[captured][to];

    int final_piece = m.promo ? make_piece(us, m.promo) : p;
    b.sq[to] = final_piece;
    b.hash  ^= ZOB_PIECE[final_piece][to];

    if (type==KING) b.king_sq[us] = to;

    // En-passant capture: remove the captured pawn
    if (m.flags & FLAG_EP) {
        int ep_sq = to + (us==WHITE ? -8 : 8);
        u.captured = b.sq[ep_sq];          // overwrite to store the pawn
        b.hash    ^= ZOB_PIECE[b.sq[ep_sq]][ep_sq];
        b.sq[ep_sq] = EMPTY;
    }

    // Castling: slide the rook
    if (m.flags & FLAG_CASTLE) {
        int rf = (us==WHITE) ? ((to==6)?7:0) : ((to==62)?63:56);
        int rt = (us==WHITE) ? ((to==6)?5:3) : ((to==62)?61:59);
        int rook = b.sq[rf];
        b.hash ^= ZOB_PIECE[rook][rf] ^ ZOB_PIECE[rook][rt];
        b.sq[rt]=rook; b.sq[rf]=EMPTY;
    }

    // Update castling rights, en-passant, halfmove
    b.castling &= CASTLE_MASK[from] & CASTLE_MASK[to];
    b.ep = (type==PAWN && std::abs(to-from)==16) ? (from+to)/2 : -1;
    b.halfmove = (type==PAWN || captured) ? 0 : b.halfmove+1;
    if (us==BLACK) ++b.fullmove;

    // Flip side
    b.side = them;
    b.hash ^= ZOB_SIDE;

    // Add new castling & ep contributions
    if (b.ep>=0) b.hash ^= ZOB_EP[b.ep];
    b.hash ^= ZOB_CASTLE[b.castling];
}

void unmake_move(Board& b, const Move& m) {
    Undo& u = b.hist[--b.ply];
    b.side = 1-b.side;                 // restore the mover
    int us=b.side, them=1-us;

    int final_piece = b.sq[m.to];
    int orig_piece  = m.promo ? make_piece(us, PAWN) : final_piece;

    b.sq[m.from] = orig_piece;
    b.sq[m.to]   = u.captured;         // restore captured piece (EMPTY if none)

    if (piece_type(orig_piece)==KING) b.king_sq[us] = m.from;

    if (m.flags & FLAG_EP) {
        int ep_sq = m.to + (us==WHITE ? -8 : 8);
        b.sq[ep_sq] = make_piece(them, PAWN);
        b.sq[m.to]  = EMPTY;
    }

    if (m.flags & FLAG_CASTLE) {
        int rf = (us==WHITE) ? ((m.to==6)?7:0) : ((m.to==62)?63:56);
        int rt = (us==WHITE) ? ((m.to==6)?5:3) : ((m.to==62)?61:59);
        b.sq[rf]=b.sq[rt]; b.sq[rt]=EMPTY;
    }

    if (us==BLACK) --b.fullmove;
    b.ep=u.ep; b.castling=u.castling;
    b.halfmove=u.halfmove; b.hash=u.hash;
}

// =============================================================================
// EVALUATION  (material + PST, from side-to-move's perspective)
// =============================================================================
int evaluate(const Board& b) {
    int score = 0;
    for (int sq=0; sq<64; ++sq) {
        int p=b.sq[sq];
        if (!p) continue;
        int type  = piece_type(p);
        int color = piece_color(p);
        int psq   = (color==WHITE) ? sq : mirror_sq(sq);
        int val   = PIECE_VALUE[type] + PST[type][psq];
        score    += (color==WHITE) ? val : -val;
    }
    return (b.side==WHITE) ? score : -score;
}

// =============================================================================
// TRANSPOSITION  TABLE   (keyed on the board's own Zobrist hash, ~64 MB)
// =============================================================================
static const int TT_BITS = 22;             // 4 M entries
static const int TT_SIZE = 1 << TT_BITS;
static const int TT_MASK = TT_SIZE - 1;

enum TTFlag : uint8_t { TT_NONE=0, TT_EXACT, TT_LOWER, TT_UPPER };

struct TTEntry {
    uint64_t key   = 0;
    int      score = 0;
    int      depth = -1;
    TTFlag   flag  = TT_NONE;
    Move     best  = {};
};

static TTEntry TT[TT_SIZE];

void tt_clear() { for (auto& e : TT) e = TTEntry{}; }

void tt_store(uint64_t key, int depth, int score, TTFlag flag, const Move& best) {
    TTEntry& e = TT[key & TT_MASK];
    // Replace unless the existing entry is for the same position, was
    // searched deeper, and the new entry isn't an exact score.
    if (e.key == key && e.depth > depth && flag != TT_EXACT) return;
    e = { key, score, depth, flag, best };
}

// Returns true if we can use the TT value directly.
// Always sets best_move if the key matches (even if depth insufficient),
// so it can still be used for move ordering.
bool tt_probe(uint64_t key, int depth, int alpha, int beta,
              int& out_score, Move& out_best)
{
    TTEntry& e = TT[key & TT_MASK];
    if (e.key != key) return false;
    out_best = e.best;
    if (e.depth >= depth) {
        out_score = e.score;
        if (e.flag == TT_EXACT) return true;
        if (e.flag == TT_LOWER && out_score >= beta)  return true;
        if (e.flag == TT_UPPER && out_score <= alpha) return true;
    }
    return false;
}

// =============================================================================
// SEARCH  ENGINE
// =============================================================================
static const int INF    = 1'000'000;
static const int MATE   = 900'000;
static const int MAX_SD = 64;           // max search depth

static Move   killers[MAX_PLY][2];
static int    history_tbl[2][64][64];
static int    node_count;
static int    allotted_ms;
static std::chrono::steady_clock::time_point search_start;
static std::atomic<bool> stop_flag{false};

// Check for time / stop signal. stop_flag is checked on every node so "stop"
// is responsive immediately; the wall-clock read only happens every 4096
// nodes to keep that check cheap.
static bool time_up() {
    if (stop_flag.load(std::memory_order_relaxed)) return true;
    if (node_count & 4095) return false;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - search_start).count();
    return ms >= allotted_ms;
}

// Score move for ordering:
//   TT move > captures (MVV-LVA) > killer 1 > killer 2 > history
static int score_move(const Board& b, const Move& m,
                       const Move& tt_move, int ply)
{
    if (m == tt_move) return 2'000'000;
    bool ep  = (m.flags & FLAG_EP);
    bool cap = b.sq[m.to] || ep;
    if (cap) {
        int victim   = ep ? PAWN : piece_type(b.sq[m.to]);
        int attacker = piece_type(b.sq[m.from]);
        return 1'000'000 + victim * 10 - attacker;
    }
    if (ply < MAX_PLY) {
        if (m == killers[ply][0]) return 900'001;
        if (m == killers[ply][1]) return 900'000;
    }
    int col = piece_color(b.sq[m.from]);
    return history_tbl[col][m.from][m.to];
}

// ---- Quiescence search ----
static int quiesce(Board& b, int alpha, int beta) {
    ++node_count;
    if (time_up()) return 0;

    int stand_pat = evaluate(b);
    if (stand_pat >= beta)  return beta;
    if (stand_pat > alpha) alpha = stand_pat;

    MoveList ml;
    gen_moves(b, ml, /*captures_only=*/true);

    // Sort by MVV-LVA inline (selection sort over a small list of captures)
    Move dummy{};
    for (int i=0; i<ml.cnt; ++i) {
        int best_idx=i;
        int best_sc = score_move(b, ml.moves[i], dummy, 0);
        for (int j=i+1; j<ml.cnt; ++j) {
            int sc = score_move(b, ml.moves[j], dummy, 0);
            if (sc > best_sc) { best_sc=sc; best_idx=j; }
        }
        if (best_idx!=i) std::swap(ml.moves[i], ml.moves[best_idx]);

        make_move(b, ml.moves[i]);
        if (in_check(b, 1-b.side)) { unmake_move(b, ml.moves[i]); continue; }

        int score = -quiesce(b, -beta, -alpha);
        unmake_move(b, ml.moves[i]);

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

// ---- Alpha-beta negamax ----
static int negamax(Board& b, int depth, int alpha, int beta, int ply) {
    ++node_count;
    if (time_up()) return 0;

    // 50-move draw
    if (b.halfmove >= 100) return 0;

    bool in_chk = in_check(b, b.side);
    if (in_chk && depth < MAX_SD) ++depth;   // check extension

    if (depth <= 0) return quiesce(b, alpha, beta);

    // Transposition table
    int  tt_sc = 0;
    Move tt_mv = {};
    if (tt_probe(b.hash, depth, alpha, beta, tt_sc, tt_mv))
        return tt_sc;

    // Generate moves and score them
    MoveList ml;
    gen_moves(b, ml);

    int  scores[256];
    for (int i=0; i<ml.cnt; ++i)
        scores[i] = score_move(b, ml.moves[i], tt_mv, ply);

    int  best_score = -INF;
    Move best_move  = {};
    int  legal_cnt  = 0;
    TTFlag flag     = TT_UPPER;

    for (int i=0; i<ml.cnt; ++i) {
        // Selection sort: bring highest-scored move to position i
        for (int j=i+1; j<ml.cnt; ++j)
            if (scores[j] > scores[i]) {
                std::swap(ml.moves[i], ml.moves[j]);
                std::swap(scores[i],  scores[j]);
            }

        const Move& move = ml.moves[i];
        bool is_cap = b.sq[move.to] || (move.flags & FLAG_EP);

        make_move(b, move);
        if (in_check(b, 1-b.side)) { unmake_move(b, move); continue; }
        ++legal_cnt;

        int score = -negamax(b, depth-1, -beta, -alpha, ply+1);
        unmake_move(b, move);

        if (time_up()) return 0;

        if (score > best_score) { best_score=score; best_move=move; }
        if (score > alpha)      { alpha=score; flag=TT_EXACT; }

        if (alpha >= beta) {
            // Beta cutoff — update killers and history for quiet moves
            if (!is_cap && ply < MAX_PLY) {
                if (!(move==killers[ply][0])) {
                    killers[ply][1] = killers[ply][0];
                    killers[ply][0] = move;
                }
                int col = b.side;   // b.side is restored after unmake
                if (history_tbl[col][move.from][move.to] < 30000)
                    history_tbl[col][move.from][move.to] += depth*depth;
            }
            flag = TT_LOWER;
            break;
        }
    }

    if (!legal_cnt) return in_chk ? -MATE + ply : 0;   // checkmate / stalemate

    tt_store(b.hash, depth, best_score, flag, best_move);
    return best_score;
}

// ---- Root search: returns (score, best_move) ----
struct RootResult { int score; Move best_move; };

static RootResult search_root(Board& b, int depth) {
    // Fetch TT move for ordering (no early return at root)
    Move tt_mv = {};
    {
        TTEntry& e = TT[b.hash & TT_MASK];
        if (e.key == b.hash) tt_mv = e.best;
    }

    MoveList ml;
    gen_moves(b, ml);

    int  scores[256];
    for (int i=0; i<ml.cnt; ++i)
        scores[i] = score_move(b, ml.moves[i], tt_mv, 0);

    int  best_score = -INF;
    Move best_move  = {};

    for (int i=0; i<ml.cnt; ++i) {
        for (int j=i+1; j<ml.cnt; ++j)
            if (scores[j] > scores[i]) {
                std::swap(ml.moves[i], ml.moves[j]);
                std::swap(scores[i],  scores[j]);
            }

        const Move& move = ml.moves[i];
        make_move(b, move);
        if (in_check(b, 1-b.side)) { unmake_move(b, move); continue; }

        int score = -negamax(b, depth-1, -INF, INF, 1);
        unmake_move(b, move);

        if (time_up()) {
            // If we haven't found any move yet, use this partial result as fallback
            if (best_move.is_none()) { best_move=move; best_score=score; }
            break;
        }
        if (score > best_score) { best_score=score; best_move=move; }
    }

    tt_store(b.hash, depth, best_score, TT_EXACT, best_move);
    return { best_score, best_move };
}

// ---- UCI move helpers ----
static std::string move_to_uci(const Move& m) {
    if (m.is_none()) return "0000";
    char buf[6] = {};
    buf[0] = 'a' + file_of(m.from);
    buf[1] = '1' + rank_of(m.from);
    buf[2] = 'a' + file_of(m.to);
    buf[3] = '1' + rank_of(m.to);
    if (m.promo) {
        const char pc[] = ".nbrq";
        buf[4] = pc[m.promo];
    }
    return std::string(buf);
}

static Move uci_to_move(const Board& b, const std::string& s) {
    if (s.size() < 4) return {};
    int from = (s[1]-'1')*8 + (s[0]-'a');
    int to   = (s[3]-'1')*8 + (s[2]-'a');
    int promo = 0;
    if (s.size()>=5) {
        switch(s[4]) {
            case 'n': promo=KNIGHT; break; case 'b': promo=BISHOP; break;
            case 'r': promo=ROOK;   break; case 'q': promo=QUEEN;  break;
        }
    }
    int flags = 0;
    int p = b.sq[from];
    if (piece_type(p)==PAWN && to==b.ep && b.ep>=0)              flags=FLAG_EP;
    if (piece_type(p)==KING && std::abs(file_of(to)-file_of(from))==2) flags=FLAG_CASTLE;
    return {(uint8_t)from,(uint8_t)to,(uint8_t)promo,(uint8_t)flags};
}

// ---- Iterative deepening with time control ----
// On timeout mid-depth, the partial result from that depth is discarded and
// the best move from the last *completed* depth is returned instead.
static Move iterative_deepening(Board b, int time_ms) {
    search_start = std::chrono::steady_clock::now();
    allotted_ms  = time_ms;
    stop_flag    = false;
    node_count   = 0;
    b.ply        = 0;

    memset(killers,     0, sizeof(killers));
    memset(history_tbl, 0, sizeof(history_tbl));

    Move best_move  = {};
    int  best_score = 0;

    // Fallback: if time is extremely short, just pick first legal move
    {
        MoveList ml; gen_moves(b, ml);
        for (int i=0; i<ml.cnt; ++i) {
            make_move(b, ml.moves[i]);
            if (!in_check(b, 1-b.side)) {
                unmake_move(b, ml.moves[i]);
                best_move = ml.moves[i];
                break;
            }
            unmake_move(b, ml.moves[i]);
        }
    }

    for (int depth = 1; depth <= MAX_SD; ++depth) {
        auto res = search_root(b, depth);

        // Discard partial result if time ran out mid-search (keep previous depth)
        if (time_up() && depth > 1) break;

        if (!res.best_move.is_none()) {
            best_move  = res.best_move;
            best_score = res.score;
        }

        // Print UCI info line
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - search_start).count();

        std::string score_str;
        if (std::abs(best_score) >= MATE - MAX_PLY) {
            int N = MATE - std::abs(best_score);
            int n = (N+1)/2;
            score_str = "mate " + std::to_string(best_score>0 ? n : -n);
        } else {
            score_str = "cp " + std::to_string(best_score);
        }

        std::cout << "info depth " << depth
                  << " score "     << score_str
                  << " nodes "     << node_count
                  << " time "      << elapsed
                  << " nps "       << (elapsed>0 ? (long long)node_count*1000/elapsed : 0)
                  << " pv "        << move_to_uci(best_move)
                  << "\n" << std::flush;

        if (std::abs(best_score) >= MATE - MAX_PLY) break; // forced mate found
        if (time_up()) break;
    }
    return best_move;
}

// =============================================================================
// UCI  PROTOCOL  LOOP
// =============================================================================
static Board       main_board;
static std::thread search_thread;

static void position_cmd(const std::string& line) {
    std::istringstream ss(line);
    std::string tok;
    ss >> tok;   // "position"
    ss >> tok;   // "startpos" or "fen"

    if (tok == "startpos") {
        load_fen(main_board,
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    } else {
        // "fen" — read tokens until "moves" or stream ends
        std::string fen;
        while (ss >> tok && tok != "moves") {
            if (!fen.empty()) fen += " ";
            fen += tok;
        }
        load_fen(main_board, fen);
        if (tok == "moves") {
            while (ss >> tok) {
                Move m = uci_to_move(main_board, tok);
                if (!m.is_none()) make_move(main_board, m);
            }
        }
        return;
    }

    // For startpos, advance past optional "moves" keyword
    if (ss >> tok && tok == "moves") {
        while (ss >> tok) {
            Move m = uci_to_move(main_board, tok);
            if (!m.is_none()) make_move(main_board, m);
        }
    }
}

static void go_cmd(const std::string& line) {
    std::istringstream ss(line);
    std::string tok;
    ss >> tok;   // "go"

    int movetime=-1, wtime=-1, btime=-1, winc=0, binc=0, movestogo=40;
    while (ss >> tok) {
        if      (tok=="movetime")   ss >> movetime;
        else if (tok=="wtime")      ss >> wtime;
        else if (tok=="btime")      ss >> btime;
        else if (tok=="winc")       ss >> winc;
        else if (tok=="binc")       ss >> binc;
        else if (tok=="movestogo")  ss >> movestogo;
    }

    int time_ms;
    if (movetime >= 0) {
        // Use 95% of the given time to leave a tiny safety margin
        time_ms = movetime * 95 / 100;
    } else {
        int my_time = (main_board.side==WHITE) ? wtime : btime;
        int my_inc  = (main_board.side==WHITE) ? winc  : binc;
        if (my_time <= 0) my_time = 5000;
        if (movestogo <= 0) movestogo = 40;

        // Simple formula: remaining_time / moves_left + 80% of increment
        time_ms = my_time / movestogo + my_inc * 4 / 5;
        time_ms = std::min(time_ms, my_time * 45 / 100);  // never > 45% remaining
        time_ms = std::max(time_ms, 50);                  // at least 50 ms
    }

    // Join any previous search thread
    stop_flag = true;
    if (search_thread.joinable()) search_thread.join();
    stop_flag = false;

    // Launch search in background thread; print bestmove when done.
    // Running the search on its own thread (rather than blocking the main
    // UCI loop) means "stop" arriving on stdin is processed immediately.
    Board board_copy = main_board;
    search_thread = std::thread([board_copy, time_ms]() mutable {
        Move best = iterative_deepening(board_copy, time_ms);
        std::cout << "bestmove " << move_to_uci(best) << "\n" << std::flush;
    });
}

void uci_loop() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;

        if (cmd == "uci") {
            std::cout << "id name WeekFourEngine\n"
                      << "id author ClubMember\n"
                      << "uciok\n" << std::flush;

        } else if (cmd == "isready") {
            // Wait for any running search to finish if needed
            std::cout << "readyok\n" << std::flush;

        } else if (cmd == "ucinewgame") {
            stop_flag = true;
            if (search_thread.joinable()) search_thread.join();
            load_fen(main_board,
                "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
            tt_clear();
            stop_flag = false;

        } else if (cmd == "position") {
            position_cmd(line);

        } else if (cmd == "go") {
            go_cmd(line);

        } else if (cmd == "stop") {
            stop_flag = true;
            if (search_thread.joinable()) search_thread.join();
            stop_flag = false;

        } else if (cmd == "quit") {
            stop_flag = true;
            if (search_thread.joinable()) search_thread.join();
            return;
        }
        // Unknown commands are silently ignored (conforming to UCI spec)
    }
}

// =============================================================================
// ENTRY  POINT
// =============================================================================
int main() {
    // Disable sync for faster I/O
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    init_zobrist();
    tt_clear();
    load_fen(main_board,
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    uci_loop();
    return 0;
}