package vn.pawbound.game;

import org.libsdl.app.SDLActivity;

/** Activity của PAWBOUND: SDL3 được liên kết tĩnh vào libmain.so nên chỉ cần nạp "main". */
public class PawboundActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }
}
