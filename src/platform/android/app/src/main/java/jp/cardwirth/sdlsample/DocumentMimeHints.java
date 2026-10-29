package jp.cardwirth.sdlsample;

import java.util.LinkedHashSet;
import java.util.Locale;
import java.util.function.Function;

/** ネイティブの候補情報への情報を失う変換。選択文書の検証処理ではない。 */
final class DocumentMimeHints {
    private DocumentMimeHints() {}

    static String[] collect(String[] extensions, Function<String, String> resolver) {
        if (extensions == null || extensions.length == 0) return new String[0];
        final LinkedHashSet<String> types = new LinkedHashSet<>();
        for (String extension : extensions) {
            if (extension == null || extension.isEmpty()) return new String[0];
            final String mime = resolver.apply(extension.toLowerCase(Locale.ROOT));
            // 表現できない拡張子が一つでもあれば、制限なしの選択にする。MIME
            // の和集合を狭めると、呼び出し元が許可した文書を隠してしまう。
            if (mime == null || mime.isEmpty()) return new String[0];
            types.add(mime);
        }
        return types.toArray(new String[0]);
    }
}
