#pragma once
// Implemented through the shared GUI.Forms window cursor service.
// No game-local native host or desktop-coordinate API belongs here.
namespace sbx {
void cursor_set_hidden(bool hidden);
void cursor_warp(double x, double y);
}
