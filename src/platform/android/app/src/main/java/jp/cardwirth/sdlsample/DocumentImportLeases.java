package jp.cardwirth.sdlsample;

import java.io.File;
import java.io.IOException;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/** プロバイダーが所有するパスだけを扱う。この登録表自体はファイルを削除しない。 */
final class DocumentImportLeases {
    private final Map<String, Batch> paths = new HashMap<>();
    private final Set<File> roots = new HashSet<>();

    synchronized void register(File directory, String[] selected) throws IOException {
        final File root = directory.getCanonicalFile();
        if (!root.equals(directory.getAbsoluteFile()) || roots.contains(root) ||
                selected == null || selected.length == 0)
            throw new IOException("Invalid import staging root");
        final Batch batch = new Batch(root);
        for (String value : selected) {
            if (value == null || value.isEmpty()) throw new IOException("Empty import path");
            final File path = new File(value);
            // 別名、兄弟、シンボリックリンク、準備領域のルート自身を拒否す
            // る。プロバイダーは選択した各文書を直下の子として準備する。
            if (!path.isAbsolute() || !path.equals(path.getCanonicalFile()) ||
                    !root.equals(path.getParentFile()) || paths.containsKey(value) ||
                    !batch.remaining.add(value))
                throw new IOException("Invalid import staging path");
        }
        // 所有権を公開する前に、一群全体を検証する。
        roots.add(root);
        for (String value : batch.remaining) paths.put(value, batch);
    }

    // 一群の最後のパスを解放するときだけ、その専用ディレクトリーも解放
    // する。未知のパス、重複解放、子孫のパスには削除権限を与えない。
    synchronized File release(String selected) {
        final Batch batch = paths.remove(selected);
        if (batch == null) return null;
        batch.remaining.remove(selected);
        if (!batch.remaining.isEmpty()) return null;
        roots.remove(batch.root);
        return batch.root;
    }

    // Activity の終了処理は、登録中の全準備ルートを所有する。
    // スナップショットを返し、個別パスの利用権を不可分にすべて取り消し
    // て、遅れたネイティブの解放が削除権限を二重に得ないようにする。
    synchronized Set<File> drainRoots() {
        final Set<File> result = new HashSet<>(roots);
        paths.clear();
        roots.clear();
        return result;
    }

    private static final class Batch {
        final File root;
        final Set<String> remaining = new HashSet<>();
        Batch(File root) { this.root = root; }
    }
}
