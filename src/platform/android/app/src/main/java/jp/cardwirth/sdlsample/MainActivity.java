package jp.cardwirth.sdlsample;

import android.content.Context;
import android.content.res.AssetManager;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.ApplicationInfo;
import android.media.MediaPlayer;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.MediaController;
import android.widget.Toast;
import android.widget.VideoView;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.BufferedReader;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.HashMap;
import java.util.Map;
import android.util.Base64;

public final class MainActivity extends SDLActivity {
    // SDLActivity は既定で libSDL3.so と libmain.so を読み込む。

    /**
     * adb の統合検査用のデバッグ専用連携。通常の SDLActivity
     * は空の argv を渡すため、実 Android 端末でネイティブ
     * の --smoke-test 経路に到達できない。Release
     * APK は、別アプリが公開ランチャーへ追加情報を送っても無視する。
     */
    public static final String TEST_ARGUMENTS_EXTRA =
            "jp.cardwirth.cardwirthc.test.ARGUMENTS";

    private VideoView videoView;
    private CardWirthDocumentPicker documentPicker;

    /** ネイティブの ALL のダウンロードワーカーから呼ぶ。UI スレッドでは呼ばない。 */
    public boolean cardwirthDownloadToFile(String address, String destination, long limit) {
        HttpURLConnection connection = null;
        File output = new File(destination);
        boolean success = false;
        try {
            URL url = new URL(address);
            if (!"https".equals(url.getProtocol()) ||
                    !"webcardwirth.net".equalsIgnoreCase(url.getHost())) return false;
            connection = (HttpURLConnection) url.openConnection();
            connection.setConnectTimeout(10000);
            connection.setReadTimeout(30000);
            connection.setInstanceFollowRedirects(false);
            if (connection.getResponseCode() != HttpURLConnection.HTTP_OK ||
                    connection.getContentLengthLong() > limit) return false;
            long count = 0;
            try (InputStream input = connection.getInputStream();
                 FileOutputStream stream = new FileOutputStream(output)) {
                byte[] buffer = new byte[65536];
                int read;
                while ((read = input.read(buffer)) != -1) {
                    count += read;
                    if (count > limit) return false;
                    stream.write(buffer, 0, read);
                }
                stream.getFD().sync();
            }
            success = count > 0;
            return success;
        } catch (Exception error) {
            Log.e("CardWirthC", "Additional data download failed", error);
            return false;
        } finally {
            if (connection != null) connection.disconnect();
            if (!success && output.exists()) output.delete();
        }
    }

    @Override
    protected String[] getArguments() {
        if ((getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0) {
            return super.getArguments();
        }
        final Intent intent = getIntent();
        final String[] arguments = intent == null
                ? null : intent.getStringArrayExtra(TEST_ARGUMENTS_EXTRA);
        return arguments == null ? super.getArguments() : arguments;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // アプリは横向き固定。センサーに従い左右どちらも許可する。
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        super.onCreate(savedInstanceState);
        hideSystemBars();
        // SDLActivity は super.onCreate() 内で初期の
        // setWindowStyle(false) をキューへ入れる。その処
        // 理後に方針を再適用し、起動中にシステムバーが再表示されないようにする。
        getWindow().getDecorView().post(this::hideSystemBars);
        documentPicker = new CardWirthDocumentPicker(this);
    }

    /** ALL の表示では Android のステータスバーとナビゲーションバーを隠したままにする。 */
    private void hideSystemBars() {
        final Window window = getWindow();
        if (window == null) {
            return;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // Android 15 は targetSdk 35 のアプリ
            // に画面端までの表示を適用する。SDL の描画面に画面全体を明
            // 示的に使わせ、没入モード中は切り欠き領域も利用可能にする。
            window.setDecorFitsSystemWindows(false);
            final WindowManager.LayoutParams attributes = window.getAttributes();
            attributes.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
            window.setAttributes(attributes);
        }
        window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        window.clearFlags(WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN);
        final View decor = window.getDecorView();
        decor.setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.INVISIBLE);
        // SDLActivity の表示監視は、このフラグが true のときだけバ
        // ーを再度隠す。初期の setWindowStyle(false) では、Activity
        // が全画面でもフラグが false になるため設定し直す。
        mFullscreenModeActive = true;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            final WindowInsetsController controller = decor.getWindowInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.statusBars() |
                                WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior(
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
    }

    @Override
    public void setOrientationBis(int width, int height, boolean resizable, String hint) {
        // SDL は通常、サイズ変更可能なウィンドウをここで FULL_USER にす
        // る。アプリは横向き専用なので、左右の横向きだけを保ち、縦向きは許可しない。
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    // Explorer と Editor の共通のネイティブ文書プロバイダー境界。
    public String[] cardwirthPickDocuments(final int kind, final String suggestedName,
                                           final boolean multiple, final String[] extensions) {
        if (documentPicker == null) {
            return null;
        }
        return documentPicker.pick(kind, suggestedName == null ? "" : suggestedName,
                                   multiple, extensions);
    }

    public String[] cardwirthDocumentExportConflicts(final String stagingPath) {
        return documentPicker == null ? null : documentPicker.exportConflicts(stagingPath);
    }

    public void cardwirthReleaseDocumentImport(final String path) throws java.io.IOException {
        if (documentPicker != null) documentPicker.releaseImport(path);
    }

    public boolean cardwirthFinalizeDocumentExport(final String stagingPath, final boolean success,
                                                   final boolean overwriteAccepted) {
        return documentPicker != null &&
            documentPicker.finalizeExport(stagingPath, success, overwriteAccepted);
    }

    public String cardwirthDocumentPickerError() {
        return documentPicker == null ? "ファイル選択を初期化できません" : documentPicker.lastError();
    }

    // CWXEditor の共通バックエンドが論理フォーカスの移動時に呼ぶ
    // 。コントロールは SDL が描くため、TalkBack がラベル・ヒ
    // ントを取得できるネイティブ View が Android 側にない。
    public void cardwirthAnnounceAccessibility(final String name, final String description) {
        final String label = name == null ? "" : name.trim();
        final String detail = description == null ? "" : description.trim();
        final String message;
        if (label.isEmpty()) {
            message = detail;
        } else if (detail.isEmpty()) {
            message = label;
        } else {
            message = label + ". " + detail;
        }
        if (message.isEmpty()) {
            return;
        }
        runOnUiThread(() -> {
            if (getWindow() != null && getWindow().getDecorView() != null) {
                getWindow().getDecorView().announceForAccessibility(message);
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (documentPicker != null && documentPicker.onActivityResult(requestCode, resultCode, data)) {
            return;
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    static void installBundledData(Context context) throws IOException {
        final File appRoot = new File(context.getFilesDir(), "app");
        final File marker = new File(appRoot, ".cardwirth-assets-version");
        final String expected = context.getString(R.string.bundled_asset_version);
        boolean assetsCurrent = false;
        if (marker.isFile()) {
            final byte[] stored = new byte[(int)Math.min(marker.length(), 256)];
            final int count;
            try (FileInputStream input = new FileInputStream(marker)) {
                count = input.read(stored);
            }
            final String actual = count < 0 ? "" : new String(stored, 0, count, StandardCharsets.UTF_8);
            if (expected.equals(actual) && new File(appRoot, "Data").isDirectory() &&
                    new File(appRoot, "resource").isDirectory()) {
                assetsCurrent = true;
            }
        }
        if (!assetsCurrent) {
            final File installedData = new File(appRoot, "Data");
            final File installedEditorResources = new File(appRoot, "resource");
            final File stagingRoot = new File(appRoot, ".cardwirth-assets-staging");
            final File backupRoot = new File(appRoot, ".cardwirth-assets-backup");
            deleteTree(stagingRoot);
            deleteTree(backupRoot);
            copyAssetTree(context.getAssets(), "Data", new File(stagingRoot, "Data"));
            copyAssetTree(context.getAssets(), "resource", new File(stagingRoot, "resource"));

            if (!backupRoot.mkdirs() && !backupRoot.isDirectory()) {
                throw new IOException("Cannot create asset backup directory: " + backupRoot);
            }
            boolean dataCommitted = false;
            boolean resourcesCommitted = false;
            try {
                replaceAssetTree(installedData, new File(stagingRoot, "Data"),
                                 new File(backupRoot, "Data"));
                dataCommitted = true;
                replaceAssetTree(installedEditorResources, new File(stagingRoot, "resource"),
                                 new File(backupRoot, "resource"));
                resourcesCommitted = true;
            } catch (IOException error) {
                if (resourcesCommitted) {
                    restoreAssetTree(installedEditorResources, new File(backupRoot, "resource"));
                }
                if (dataCommitted) {
                    restoreAssetTree(installedData, new File(backupRoot, "Data"));
                }
                throw error;
            }
            deleteTree(stagingRoot);
            deleteTree(backupRoot);
        }

        // UserData は files/ の直下に置く。以前の同梱初
        // 期データと一致するファイルだけを更新する。利用者の作成・変更し
        // たファイルを保ち、未変更の素材にはアプリ更新の修正を反映する。
        updateManagedUserDataSeed(context);

        if (!assetsCurrent) {
            if (!appRoot.isDirectory() && !appRoot.mkdirs()) {
                throw new IOException("Cannot create app asset directory: " + appRoot);
            }
            final File temporary = new File(appRoot, ".cardwirth-assets-version.tmp");
            try (FileOutputStream output = new FileOutputStream(temporary)) {
                output.write(expected.getBytes(StandardCharsets.UTF_8));
                output.getFD().sync();
            }
            if (marker.exists() && !marker.delete()) {
                throw new IOException("Cannot replace asset marker: " + marker);
            }
            if (!temporary.renameTo(marker)) {
                throw new IOException("Cannot commit asset marker: " + marker);
            }
        }
        Log.i("CardWirthPy_C", "Bundled assets ready; UserData seed ensured in " +
            context.getFilesDir());
    }

    private static void deleteTree(File path) throws IOException {
        if (!path.exists()) {
            return;
        }
        final File[] children = path.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteTree(child);
            }
        }
        if (!path.delete()) {
            throw new IOException("Cannot remove stale asset: " + path);
        }
    }

    private static void replaceAssetTree(File installed, File staged, File backup)
            throws IOException {
        if (!staged.exists()) {
            throw new IOException("Staged asset tree is missing: " + staged);
        }
        if (installed.exists() && !installed.renameTo(backup)) {
            throw new IOException("Cannot preserve installed asset tree: " + installed);
        }
        if (!staged.renameTo(installed)) {
            if (backup.exists()) backup.renameTo(installed);
            throw new IOException("Cannot commit staged asset tree: " + staged);
        }
    }

    private static void restoreAssetTree(File installed, File backup) throws IOException {
        deleteTree(installed);
        if (backup.exists() && !backup.renameTo(installed)) {
            throw new IOException("Cannot restore installed asset tree: " + installed);
        }
    }

    private static void copyAssetTree(AssetManager assets, String assetPath, File destination)
            throws IOException {
        final String[] children = assets.list(assetPath);
        if (children != null && children.length > 0) {
            if (!destination.isDirectory() && !destination.mkdirs()) {
                throw new IOException("Cannot create asset directory: " + destination);
            }
            for (String child : children) {
                copyAssetTree(assets, assetPath + "/" + child, new File(destination, child));
            }
            return;
        }
        final File parent = destination.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("Cannot create asset parent: " + parent);
        }
        final File temporary = new File(destination.getPath() + ".tmp");
        try (InputStream input = assets.open(assetPath);
             FileOutputStream output = new FileOutputStream(temporary)) {
            final byte[] buffer = new byte[64 * 1024];
            int count;
            while ((count = input.read(buffer)) >= 0) {
                output.write(buffer, 0, count);
            }
            output.getFD().sync();
        }
        if (destination.exists() && !destination.delete()) {
            throw new IOException("Cannot replace asset file: " + destination);
        }
        if (!temporary.renameTo(destination)) {
            throw new IOException("Cannot commit asset file: " + destination);
        }
    }

    private static void copyUserDataSeedFile(
            AssetManager assets, String relative, File destination, boolean replace)
            throws IOException {
        final File parent = destination.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("Cannot create UserData seed parent: " + parent);
        }
        final File temporary = new File(destination.getPath() + ".tmp");
        final File backup = new File(destination.getPath() + ".cardwirth-old");
        if (!destination.exists() && backup.exists() && !backup.renameTo(destination)) {
            throw new IOException("Cannot restore managed UserData seed: " + destination);
        }
        if (temporary.exists() && !temporary.delete()) {
            throw new IOException("Cannot clear UserData seed temporary file: " + temporary);
        }
        try (InputStream input = assets.open("UserDataSeed/" + relative);
             FileOutputStream output = new FileOutputStream(temporary)) {
            final byte[] buffer = new byte[64 * 1024];
            int count;
            while ((count = input.read(buffer)) >= 0) {
                output.write(buffer, 0, count);
            }
            output.getFD().sync();
        }
        if (destination.exists() && !replace) {
            if (!temporary.delete()) {
                throw new IOException("Cannot discard raced UserData seed: " + temporary);
            }
            return;
        }
        boolean backedUp = false;
        if (destination.exists()) {
            if (backup.exists() && !backup.delete()) {
                temporary.delete();
                throw new IOException("Cannot clear managed UserData backup: " + backup);
            }
            if (!destination.renameTo(backup)) {
                temporary.delete();
                throw new IOException("Cannot preserve managed UserData seed: " + destination);
            }
            backedUp = true;
        }
        if (!temporary.renameTo(destination)) {
            if (backedUp) backup.renameTo(destination);
            throw new IOException("Cannot commit UserData seed file: " + destination);
        }
        if (backedUp && !backup.delete())
            Log.w("CardWirthPy_C", "Stale managed UserData backup remains: " + backup);
    }

    private static Map<String, String> readUserDataSeedManifest(InputStream stream)
            throws IOException {
        final Map<String, String> result = new HashMap<>();
        try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(stream, StandardCharsets.UTF_8))) {
            if (!"CWANDROIDUSERDATA1".equals(reader.readLine())) return result;
            String line;
            while ((line = reader.readLine()) != null) {
                final int separator = line.indexOf('\t');
                if (separator <= 0 || separator + 1 >= line.length()) continue;
                try {
                    final String path = new String(Base64.decode(
                        line.substring(0, separator), Base64.DEFAULT), StandardCharsets.UTF_8);
                    result.put(path, line.substring(separator + 1));
                } catch (IllegalArgumentException ignored) {
                    // 不正な行は無視し、置換を許可する根拠にしない。
                }
            }
        }
        return result;
    }

    private static String sha256(File file) throws IOException {
        final MessageDigest digest;
        try {
            digest = MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException error) {
            throw new IOException("SHA-256 is unavailable", error);
        }
        try (InputStream input = new FileInputStream(file)) {
            final byte[] buffer = new byte[64 * 1024];
            int count;
            while ((count = input.read(buffer)) >= 0) digest.update(buffer, 0, count);
        }
        final StringBuilder result = new StringBuilder(64);
        for (byte value : digest.digest()) result.append(String.format("%02x", value & 0xff));
        return result.toString();
    }

    private static File managedUserDataPath(File root, String relative) throws IOException {
        if (relative.isEmpty() || relative.startsWith("/") || relative.startsWith("\\") ||
                relative.contains("\\") || relative.indexOf('\0') >= 0) {
            throw new IOException("Invalid managed UserData path: " + relative);
        }
        for (String component : relative.split("/", -1)) {
            if (component.isEmpty() || component.equals(".") || component.equals(".."))
                throw new IOException("Invalid managed UserData path: " + relative);
        }
        final File destination = new File(root, relative);
        final String rootPath = root.getCanonicalPath() + File.separator;
        if (!destination.getCanonicalPath().startsWith(rootPath))
            throw new IOException("Managed UserData path escapes root: " + relative);
        return destination;
    }

    private static void writeUserDataSeedManifest(File destination, byte[] contents)
            throws IOException {
        final File temporary = new File(destination.getPath() + ".tmp");
        final File backup = new File(destination.getPath() + ".cardwirth-old");
        if (!destination.exists() && backup.exists() && !backup.renameTo(destination))
            throw new IOException("Cannot restore UserData seed manifest: " + destination);
        try (FileOutputStream output = new FileOutputStream(temporary)) {
            output.write(contents);
            output.getFD().sync();
        }
        boolean backedUp = false;
        if (destination.exists()) {
            if (backup.exists() && !backup.delete()) {
                temporary.delete();
                throw new IOException("Cannot clear UserData seed manifest backup: " + backup);
            }
            if (!destination.renameTo(backup)) {
                temporary.delete();
                throw new IOException("Cannot preserve UserData seed manifest: " + destination);
            }
            backedUp = true;
        }
        if (!temporary.renameTo(destination)) {
            if (backedUp) backup.renameTo(destination);
            throw new IOException("Cannot commit UserData seed manifest: " + destination);
        }
        if (backedUp && !backup.delete())
            Log.w("CardWirthPy_C", "Stale UserData seed manifest backup remains: " + backup);
    }

    private static void updateManagedUserDataSeed(Context context) throws IOException {
        final AssetManager assets = context.getAssets();
        final byte[] currentBytes;
        try (InputStream input = assets.open("UserDataSeed.manifest")) {
            final ByteArrayOutputStream contents = new ByteArrayOutputStream();
            final byte[] buffer = new byte[16 * 1024];
            int count;
            while ((count = input.read(buffer)) >= 0) contents.write(buffer, 0, count);
            currentBytes = contents.toByteArray();
        }
        final Map<String, String> current = readUserDataSeedManifest(
            new ByteArrayInputStream(currentBytes));
        if (current.isEmpty()) throw new IOException("Packaged UserData seed manifest is invalid");

        final File manifest = new File(context.getFilesDir(),
            "app/.cardwirth-userdata-manifest.tsv");
        final Map<String, String> previous = manifest.isFile()
            ? readUserDataSeedManifest(new FileInputStream(manifest)) : new HashMap<>();
        final File root = context.getFilesDir();
        for (Map.Entry<String, String> item : current.entrySet()) {
            final File destination = managedUserDataPath(root, item.getKey());
            if (!destination.exists()) {
                copyUserDataSeedFile(assets, item.getKey(), destination, false);
                continue;
            }
            final String oldHash = previous.get(item.getKey());
            if (oldHash != null && !oldHash.equals(item.getValue()) &&
                    destination.isFile() && oldHash.equals(sha256(destination))) {
                copyUserDataSeedFile(assets, item.getKey(), destination, true);
            }
        }
        for (Map.Entry<String, String> item : previous.entrySet()) {
            if (current.containsKey(item.getKey())) continue;
            final File destination = managedUserDataPath(root, item.getKey());
            if (destination.isFile() && item.getValue().equals(sha256(destination)) &&
                    !destination.delete()) {
                throw new IOException("Cannot remove retired UserData seed: " + destination);
            }
        }
        writeUserDataSeedManifest(manifest, currentBytes);
    }

    // Explorer の共通プレビューサービスから、SDL の JNI 連携を通じて呼ぶ。
    public boolean playVideo(final String path) {
        if (path == null || path.isEmpty()) {
            return false;
        }
        runOnUiThread(() -> {
            closeVideo();
            final VideoView view = new VideoView(this);
            videoView = view;
            final MediaController controls = new MediaController(this);
            controls.setAnchorView(view);
            view.setMediaController(controls);
            view.setVideoPath(path);
            view.setOnPreparedListener(MediaPlayer::start);
            view.setOnCompletionListener(player -> closeVideo());
            view.setOnErrorListener((player, what, extra) -> {
                Toast.makeText(this, "動画を再生できません", Toast.LENGTH_SHORT).show();
                closeVideo();
                return true;
            });
            addContentView(view, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
            view.requestFocus();
            view.start();
        });
        return true;
    }

    private void closeVideo() {
        if (videoView == null) {
            return;
        }
        final VideoView closing = videoView;
        videoView = null;
        closing.stopPlayback();
        final ViewGroup parent = (ViewGroup)closing.getParent();
        if (parent != null) {
            parent.removeView(closing);
        }
    }

    @Override
    public void onBackPressed() {
        if (videoView != null) {
            closeVideo();
            return;
        }
        super.onBackPressed();
    }

    @Override
    protected void onDestroy() {
        closeVideo();
        if (documentPicker != null) {
            documentPicker.shutdown();
        }
        super.onDestroy();
    }
}
