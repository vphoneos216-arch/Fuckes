package com.idragoncheats.studiopro;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;

/**
 * Thin wrapper around `su -c <command>`. Every call blocks (uses
 * Process.waitFor()) — callers must run this off the main thread.
 */
final class RootShell {

    static final class Result {
        final int exitCode;
        final String stdout;
        final String stderr;

        Result(int exitCode, String stdout, String stderr) {
            this.exitCode = exitCode;
            this.stdout = stdout;
            this.stderr = stderr;
        }

        boolean ok() {
            return exitCode == 0;
        }
    }

    private RootShell() {}

    static Result run(String command) {
        try {
            Process p = Runtime.getRuntime().exec(new String[]{"su", "-c", command});
            String out = readAll(p.getInputStream());
            String err = readAll(p.getErrorStream());
            int code = p.waitFor();
            return new Result(code, out, err);
        } catch (IOException | InterruptedException e) {
            return new Result(-1, "", "exec failed: " + e);
        }
    }

    /** Quick check: is root actually grantable right now? */
    static boolean hasRoot() {
        Result r = run("id");
        return r.ok() && r.stdout.contains("uid=0");
    }

    private static String readAll(java.io.InputStream in) throws IOException {
        BufferedReader reader = new BufferedReader(new InputStreamReader(in));
        StringBuilder sb = new StringBuilder();
        String line;
        while ((line = reader.readLine()) != null) {
            sb.append(line).append('\n');
        }
        return sb.toString();
    }
}
