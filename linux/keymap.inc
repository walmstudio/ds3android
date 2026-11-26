static void map_key(SDL_Scancode sc, int* vk, int* dik) {
    static const unsigned char dik_az[26] = { 0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C };
    static const unsigned char dik_kp[10] = { 0x4F,0x50,0x51,0x4B,0x4C,0x4D,0x47,0x48,0x49,0x52 };   /* KP_1..KP_9, KP_0 */
    *vk = 0; *dik = 0;
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) { *vk = 'A' + (sc - SDL_SCANCODE_A); *dik = dik_az[sc - SDL_SCANCODE_A]; return; }
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0) { int k = sc - SDL_SCANCODE_1; *vk = k == 9 ? '0' : '1' + k; *dik = 0x02 + k; return; }
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) { int k = sc - SDL_SCANCODE_F1; *vk = 0x70 + k; *dik = k < 10 ? 0x3B + k : 0x57 + (k - 10); return; }
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_0) { int k = sc - SDL_SCANCODE_KP_1; *vk = k == 9 ? 0x60 : 0x61 + k; *dik = dik_kp[k]; return; }
    switch (sc) {
    case SDL_SCANCODE_ESCAPE: *vk = 0x1B; *dik = 0x01; break;
    case SDL_SCANCODE_RETURN: *vk = 0x0D; *dik = 0x1C; break;
    case SDL_SCANCODE_KP_ENTER: *vk = 0x0D; *dik = 0x9C; break;
    case SDL_SCANCODE_SPACE: *vk = 0x20; *dik = 0x39; break;
    case SDL_SCANCODE_TAB: *vk = 0x09; *dik = 0x0F; break;
    case SDL_SCANCODE_BACKSPACE: *vk = 0x08; *dik = 0x0E; break;
    case SDL_SCANCODE_DELETE: *vk = 0x2E; *dik = 0xD3; break;
    case SDL_SCANCODE_INSERT: *vk = 0x2D; *dik = 0xD2; break;
    case SDL_SCANCODE_HOME: *vk = 0x24; *dik = 0xC7; break;
    case SDL_SCANCODE_END: *vk = 0x23; *dik = 0xCF; break;
    case SDL_SCANCODE_PAGEUP: *vk = 0x21; *dik = 0xC9; break;
    case SDL_SCANCODE_PAGEDOWN: *vk = 0x22; *dik = 0xD1; break;
    case SDL_SCANCODE_UP: *vk = 0x26; *dik = 0xC8; break;
    case SDL_SCANCODE_DOWN: *vk = 0x28; *dik = 0xD0; break;
    case SDL_SCANCODE_LEFT: *vk = 0x25; *dik = 0xCB; break;
    case SDL_SCANCODE_RIGHT: *vk = 0x27; *dik = 0xCD; break;
    case SDL_SCANCODE_LSHIFT: *vk = 0xA0; *dik = 0x2A; break;
    case SDL_SCANCODE_RSHIFT: *vk = 0xA1; *dik = 0x36; break;
    case SDL_SCANCODE_LCTRL: *vk = 0xA2; *dik = 0x1D; break;
    case SDL_SCANCODE_RCTRL: *vk = 0xA3; *dik = 0x9D; break;
    case SDL_SCANCODE_LALT: *vk = 0xA4; *dik = 0x38; break;
    case SDL_SCANCODE_RALT: *vk = 0xA5; *dik = 0xB8; break;
    case SDL_SCANCODE_LGUI: *vk = 0x5B; *dik = 0xDB; break;
    case SDL_SCANCODE_RGUI: *vk = 0x5C; *dik = 0xDC; break;
    case SDL_SCANCODE_APPLICATION: *vk = 0x5D; *dik = 0xDD; break;
    case SDL_SCANCODE_CAPSLOCK: *vk = 0x14; *dik = 0x3A; break;
    case SDL_SCANCODE_NUMLOCKCLEAR: *vk = 0x90; *dik = 0x45; break;
    case SDL_SCANCODE_SCROLLLOCK: *vk = 0x91; *dik = 0x46; break;
    case SDL_SCANCODE_PRINTSCREEN: *vk = 0x2C; *dik = 0xB7; break;
    case SDL_SCANCODE_PAUSE: *vk = 0x13; *dik = 0xC5; break;
    case SDL_SCANCODE_GRAVE: *vk = 0xC0; *dik = 0x29; break;      /* console */
    case SDL_SCANCODE_MINUS: *vk = 0xBD; *dik = 0x0C; break;
    case SDL_SCANCODE_EQUALS: *vk = 0xBB; *dik = 0x0D; break;
    case SDL_SCANCODE_LEFTBRACKET: *vk = 0xDB; *dik = 0x1A; break;
    case SDL_SCANCODE_RIGHTBRACKET: *vk = 0xDD; *dik = 0x1B; break;
    case SDL_SCANCODE_BACKSLASH: *vk = 0xDC; *dik = 0x2B; break;
    case SDL_SCANCODE_NONUSBACKSLASH: *vk = 0xE2; *dik = 0x56; break;
    case SDL_SCANCODE_SEMICOLON: *vk = 0xBA; *dik = 0x27; break;
    case SDL_SCANCODE_APOSTROPHE: *vk = 0xDE; *dik = 0x28; break;
    case SDL_SCANCODE_COMMA: *vk = 0xBC; *dik = 0x33; break;
    case SDL_SCANCODE_PERIOD: *vk = 0xBE; *dik = 0x34; break;
    case SDL_SCANCODE_SLASH: *vk = 0xBF; *dik = 0x35; break;
    case SDL_SCANCODE_KP_DIVIDE: *vk = 0x6F; *dik = 0xB5; break;
    case SDL_SCANCODE_KP_MULTIPLY: *vk = 0x6A; *dik = 0x37; break;
    case SDL_SCANCODE_KP_MINUS: *vk = 0x6D; *dik = 0x4A; break;
    case SDL_SCANCODE_KP_PLUS: *vk = 0x6B; *dik = 0x4E; break;
    case SDL_SCANCODE_KP_PERIOD: *vk = 0x6E; *dik = 0x53; break;
    default: break;
    }
}
