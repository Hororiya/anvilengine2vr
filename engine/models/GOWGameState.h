#pragma once
#include <engine/memory/memory_mul.h>

struct GOWGameState
{
    enum GameState {
        G_UNK,
        G_Menu,
        G_Game,
    };

    inline void setEnforceFlatScreen(bool enforce) {
        enforceFlatScreen = enforce;
        m_current_state = G_UNK;
    }
    bool enforceFlatScreen{false};

    GameState m_current_state{G_UNK};

};