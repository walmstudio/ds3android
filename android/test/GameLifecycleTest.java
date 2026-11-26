package com.ds3port.launcher;

public class GameLifecycleTest {
    public static void main(String[] args) {
        StringBuilder events = new StringBuilder();
        GameLifecycle<String> life = new GameLifecycle<>((surface, active) ->
                events.append(active ? surface : "paused").append(';'));
        life.surface("surface1");
        life.resumed(true);
        life.focused(true);
        life.focused(false);
        life.resumed(false);
        life.surface(null);
        life.surface("surface2");
        life.focused(true);
        life.resumed(true);
        life.surface(null);
        life.surface("surface3");
        if (!events.toString().equals("surface1;paused;surface2;paused;surface3;"))
            throw new AssertionError(events);
        System.out.println("ok  focus, pause and surface recreation transition only platform resources");
    }
}
