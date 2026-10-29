package jp.cardwirth.sdlsample;

import android.app.Activity;
import android.content.ClipData;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.webkit.MimeTypeMap;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.URLConnection;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Explorer と単体版・ALL の Editor で共有する Storage Access Framework の連携処理。 */
final class CardWirthDocumentPicker {
    static final int PICK_UPLOAD_FILES = 0;
    static final int PICK_UPLOAD_FOLDER = 1;
    static final int PICK_SAVE_FILE = 2;
    static final int PICK_SAVE_NAMED_FOLDER = 3;
    static final int PICK_DOWNLOAD_FOLDER = 4;

    private static final int REQUEST_CODE = 0x4357;
    private static final String DIRECTORY_MIME = DocumentsContract.Document.MIME_TYPE_DIR;

    private final MainActivity activity;
    private final Object requestLock = new Object();
    private final Map<String, PendingExport> pendingExports = new ConcurrentHashMap<>();
    private final DocumentImportLeases importLeases = new DocumentImportLeases();
    private PendingRequest pendingRequest;
    private boolean shuttingDown;
    private volatile String lastError = "";

    CardWirthDocumentPicker(MainActivity activity) {
        this.activity = activity;
    }

    String[] pick(int kind, String suggestedName, boolean multiple, String[] extensions) {
        if (kind < PICK_UPLOAD_FILES || kind > PICK_DOWNLOAD_FOLDER) {
            lastError = "未対応のファイル選択要求です";
            return null;
        }
        final PendingRequest request = new PendingRequest(kind, safeName(suggestedName),
            multiple, DocumentMimeHints.collect(extensions,
                extension -> MimeTypeMap.getSingleton().getMimeTypeFromExtension(extension)));
        synchronized (requestLock) {
            if (shuttingDown) {
                lastError = "アプリケーションが終了しました";
                return null;
            }
            if (pendingRequest != null) {
                lastError = "別のファイル選択が進行中です";
                return null;
            }
            pendingRequest = request;
        }
        lastError = "";
        activity.runOnUiThread(() -> {
            try {
                synchronized (requestLock) {
                    if (shuttingDown || pendingRequest != request || request.done.getCount() == 0) return;
                    activity.startActivityForResult(makeIntent(request), REQUEST_CODE);
                }
            } catch (RuntimeException error) {
                completeFailure(request, "ファイル選択を開始できません: " + error.getMessage());
            }
        });
        try {
            // Android の選択画面を操作可能に保ちながら、ネイティブ SDL スレッドで待機できる。
            if (!request.done.await(24, TimeUnit.HOURS)) {
                completeFailure(request, "ファイル選択がタイムアウトしました");
            }
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            completeFailure(request, "ファイル選択が中断されました");
        }
        synchronized (requestLock) {
            if (pendingRequest == request) {
                pendingRequest = null;
            }
        }
        lastError = request.error;
        return request.result;
    }

    boolean onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != REQUEST_CODE) {
            return false;
        }
        final PendingRequest request;
        synchronized (requestLock) {
            request = pendingRequest;
        }
        if (request == null) {
            return true;
        }
        if (resultCode != Activity.RESULT_OK || data == null) {
            completeEmptySelection(request);
            return true;
        }
        // Google Drive のストリームは遅い場合がある。Android の UI スレッドでプロバイダーのデータをコピーしない。
        new Thread(() -> finishSelection(request, data), "CardWirth-SAF").start();
        return true;
    }

    void releaseImport(String path) throws IOException {
        final File root = importLeases.release(path);
        if (root != null) deleteImportStaging(root);
    }

    String[] exportConflicts(String stagingPath) {
        final PendingExport export = pendingExports.get(stagingPath);
        if (export == null) {
            lastError = "書き出し先の情報が失われました";
            return null;
        }
        try {
            final List<String> conflicts = findOverwriteConflicts(new File(stagingPath), export);
            lastError = "";
            return conflicts.toArray(new String[0]);
        } catch (Exception error) {
            lastError = "保存先の競合を確認できません: " + message(error);
            return null;
        }
    }

    boolean finalizeExport(String stagingPath, boolean success, boolean overwriteAccepted) {
        final PendingExport export = pendingExports.get(stagingPath);
        if (export == null) {
            // 二度目の中止は何もしない。有効な準備領域の利用権に対応しない
            // パスは、公開済みの利用者ファイルかもしれないため削除しない。
            if (!success) {
                lastError = "";
                return true;
            }
            lastError = "書き出し先の情報が失われました";
            return false;
        }
        // 公開処理が読み取り中の準備領域を、遅れた中止で削除してはならない。独立し
        // た書き出しは別のロックを持ち、ここでは UI コールバックを呼ばない。
        synchronized (export) {
            if (pendingExports.get(stagingPath) != export) {
                lastError = success ? "書き出し先の情報が失われました" : "";
                return !success;
            }
            final File staging = new File(stagingPath);
            if (export.recoveryRequired) {
                lastError = "復旧が必要です。退避データを保持しています: " + staging.getParent();
                return false; // 中止・再試行のどちらでも、唯一のバックアップを失わないようにする。
            }
            if (!success) {
                pendingExports.remove(stagingPath, export);
                deleteTree(staging.getParentFile());
                lastError = "";
                return true;
            }
            final File rollback = new File(staging.getParentFile(), "rollback");
            Uri transactionParent = null;
            ArrayList<String> transactionNames = null;
            final ArrayList<String> changedNames = new ArrayList<>();
            try {
                final List<String> conflicts = findOverwriteConflicts(staging, export);
                if (!conflicts.isEmpty() && !overwriteAccepted) {
                    pendingExports.remove(stagingPath, export);
                    deleteTree(staging.getParentFile());
                    lastError = "書き出しをキャンセルしました";
                    return false;
                }
                if (export.exactFile) {
                    copyFileToUri(staging, export.target);
                } else {
                    Uri targetDirectory = export.target;
                    transactionParent = export.target;
                    transactionNames = new ArrayList<>();
                    if (export.createNamedDirectory) {
                        transactionNames.add(staging.getName());
                        snapshotChildren(transactionParent, transactionNames, rollback);
                        changedNames.add(staging.getName());
                        targetDirectory = replaceChild(
                            export.target, staging.getName(), DIRECTORY_MIME);
                    } else {
                        final File[] children = staging.listFiles();
                        if (children == null) throw new IOException("書き出すフォルダを読み込めません");
                        for (File child : children) transactionNames.add(child.getName());
                        snapshotChildren(transactionParent, transactionNames, rollback);
                    }
                    copyDirectoryContents(staging, targetDirectory,
                        export.createNamedDirectory ? null : changedNames);
                }
                pendingExports.remove(stagingPath, export);
                deleteTree(staging.getParentFile());
                lastError = "";
                return true;
            } catch (Exception error) {
                if (transactionParent != null && !changedNames.isEmpty()) {
                    try {
                        restoreChildren(transactionParent, changedNames, rollback);
                    } catch (Exception rollbackError) {
                        export.recoveryRequired = true;
                        error.addSuppressed(rollbackError);
                    }
                }
                // 登録済みの利用権を保ち、共通の保存トランザクションが失敗を
                // 報告してから中止するか、明示的に再試行できるようにする。プ
                // ロバイダーへの書き込み自体は不可分な置換を保証できない。
                lastError = "選択先へ書き出せません: " + message(error);
                if (export.recoveryRequired)
                    lastError += "\n復旧データを保持しています: " + staging.getParent();
                return false;
            }
        }
    }

    String lastError() {
        return lastError;
    }

    void shutdown() {
        final PendingRequest request;
        synchronized (requestLock) {
            shuttingDown = true;
            request = pendingRequest;
            pendingRequest = null;
        }
        if (request != null) {
            synchronized (request) {
                request.error = "アプリケーションが終了しました";
                request.result = null;
                request.done.countDown();
            }
        }
        for (File root : importLeases.drainRoots()) {
            try {
                deleteImportStaging(root);
            } catch (IOException ignored) {
                // 終了時の整理で、ライフサイクルの結果を置き換えてはならない。
            }
        }
        for (Map.Entry<String, PendingExport> entry : pendingExports.entrySet()) {
            final String stagingPath = entry.getKey();
            final PendingExport export = entry.getValue();
            if (!pendingExports.remove(stagingPath, export)) continue;
            synchronized (export) {
                if (!export.recoveryRequired) deleteTree(new File(stagingPath).getParentFile());
            }
        }
    }

    private List<String> findOverwriteConflicts(File staging, PendingExport export)
            throws IOException {
        final ArrayList<String> conflicts = new ArrayList<>();
        // 単一ファイルの置換方針は ACTION_CREATE_DOCUMENT
        // が担当する。フォルダー出力は CardWirthPy_C が子を作るため
        // 、replaceChild が既存項目を削除する前に確認する必要がある。
        if (export.exactFile) {
            return conflicts;
        }
        if (export.createNamedDirectory) {
            if (childExists(export.target, staging.getName())) {
                conflicts.add(staging.getName());
            }
            return conflicts;
        }
        final File[] children = staging.listFiles();
        if (children == null) {
            throw new IOException("書き出すフォルダを読み込めません");
        }
        for (File child : children) {
            if (childExists(export.target, child.getName())) {
                conflicts.add(child.getName());
            }
        }
        return conflicts;
    }

    private boolean childExists(Uri parentTree, String name) throws IOException {
        return findChild(parentTree, name) != null;
    }

    private static final class ProviderChild {
        final Uri uri;
        final String mime;
        ProviderChild(Uri uri, String mime) {
            this.uri = uri;
            this.mime = mime;
        }
    }

    private ProviderChild findChild(Uri parentTree, String name) throws IOException {
        final ContentResolver resolver = activity.getContentResolver();
        final String parentId = DocumentsContract.getDocumentId(parentTree);
        final Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(parentTree, parentId);
        final String[] columns = {
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE
        };
        try (Cursor cursor = resolver.query(children, columns, null, null, null)) {
            if (cursor == null) {
                throw new IOException("保存先フォルダの内容を取得できません");
            }
            while (cursor.moveToNext()) {
                if (name.equals(cursor.getString(1))) {
                    return new ProviderChild(
                        DocumentsContract.buildDocumentUriUsingTree(parentTree, cursor.getString(0)),
                        cursor.getString(2));
                }
            }
        }
        return null;
    }

    private void snapshotChildren(Uri parent, List<String> names, File rollback)
            throws IOException {
        deleteTree(rollback);
        if (!rollback.mkdirs() && !rollback.isDirectory())
            throw new IOException("書き出しの復元領域を作成できません");
        for (String name : names) {
            final ProviderChild child = findChild(parent, name);
            if (child == null) continue;
            final File destination = new File(rollback, safeName(name));
            if (DIRECTORY_MIME.equals(child.mime)) copyDocumentTree(parent, child.uri, destination);
            else copyUriToFile(child.uri, destination);
        }
        // 保持した書き出しは、有効な JNI 利用権がなくても再起動後に発見でき
        // る。全スナップショットの完了後、プロバイダーを変更する前に記録する。
        final StringBuilder manifest = new StringBuilder("Target: ").append(parent).append('\n');
        for (String name : names) manifest.append("Item: ").append(name).append('\n');
        try (java.io.FileOutputStream output = new java.io.FileOutputStream(
                new File(rollback.getParentFile(), "RECOVERY.txt"))) {
            output.write(manifest.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8));
            output.getFD().sync();
        }
    }

    private void restoreChildren(Uri parent, List<String> names, File rollback)
            throws IOException {
        IOException failure = null;
        for (String name : names) {
            try {
                final ProviderChild current = findChild(parent, name);
                if (current != null && !DocumentsContract.deleteDocument(
                        activity.getContentResolver(), current.uri))
                    throw new IOException("書き出し途中の項目を除去できません: " + name);
                final File backup = new File(rollback, safeName(name));
                if (backup.exists()) {
                    if (backup.isDirectory()) {
                        final Uri directory = DocumentsContract.createDocument(
                            activity.getContentResolver(), parent, DIRECTORY_MIME, name);
                        if (directory == null) throw new IOException("フォルダを復元できません: " + name);
                        copyDirectoryContents(backup, directory);
                    } else {
                        final Uri file = DocumentsContract.createDocument(
                            activity.getContentResolver(), parent, mimeForName(name), name);
                        if (file == null) throw new IOException("ファイルを復元できません: " + name);
                        copyFileToUri(backup, file);
                    }
                }
            } catch (IOException error) {
                if (failure == null) failure = error;
                else failure.addSuppressed(error);
            }
        }
        if (failure != null) throw failure;
    }

    private Intent makeIntent(PendingRequest request) {
        final Intent intent;
        switch (request.kind) {
            case PICK_UPLOAD_FILES:
                intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("*/*");
                intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, request.multiple);
                if (request.mimeTypes.length != 0) {
                    intent.putExtra(Intent.EXTRA_MIME_TYPES, request.mimeTypes);
                }
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                                Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
                return intent;
            case PICK_UPLOAD_FOLDER:
            case PICK_SAVE_NAMED_FOLDER:
            case PICK_DOWNLOAD_FOLDER:
                intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                                Intent.FLAG_GRANT_WRITE_URI_PERMISSION |
                                Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION |
                                Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
                return intent;
            case PICK_SAVE_FILE:
                intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType(mimeForName(request.suggestedName));
                intent.putExtra(Intent.EXTRA_TITLE, request.suggestedName);
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                                Intent.FLAG_GRANT_WRITE_URI_PERMISSION |
                                Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
                return intent;
            default:
                throw new IllegalArgumentException("Unsupported picker kind");
        }
    }

    private void finishSelection(PendingRequest request, Intent data) {
        try {
            switch (request.kind) {
                case PICK_UPLOAD_FILES:
                    finishUploadFiles(request, data);
                    break;
                case PICK_UPLOAD_FOLDER:
                    finishUploadFolder(request, data);
                    break;
                case PICK_SAVE_FILE:
                case PICK_SAVE_NAMED_FOLDER:
                case PICK_DOWNLOAD_FOLDER:
                    finishExportTarget(request, data);
                    break;
                default:
                    throw new IOException("未対応のファイル選択要求です");
            }
        } catch (Exception error) {
            completeFailure(request, "選択項目を読み込めません: " + message(error));
        }
    }

    private void finishUploadFiles(PendingRequest request, Intent data) throws IOException {
        final List<Uri> uris = selectedUris(data);
        if (uris.isEmpty()) {
            completeEmptySelection(request);
            return;
        }
        final File staging = newStagingDirectory("upload-files");
        boolean published = false;
        try {
            final ArrayList<String> paths = new ArrayList<>();
            for (Uri uri : uris) {
                if (!request.multiple && !paths.isEmpty()) break;
                takePermission(uri, data.getFlags(), false);
                final String name = uniqueName(staging, queryDisplayName(uri));
                final File destination = new File(staging, name);
                copyUriToFile(uri, destination);
                paths.add(destination.getAbsolutePath());
            }
            published = completeImports(request, staging, paths.toArray(new String[0]));
        } finally {
            if (!published) discardImportStaging(staging);
        }
    }

    private void finishUploadFolder(PendingRequest request, Intent data) throws IOException {
        final Uri tree = data.getData();
        if (tree == null) {
            throw new IOException("フォルダが選択されていません");
        }
        takePermission(tree, data.getFlags(), false);
        final String rootId = DocumentsContract.getTreeDocumentId(tree);
        final Uri rootDocument = DocumentsContract.buildDocumentUriUsingTree(tree, rootId);
        final File staging = newStagingDirectory("upload-folder");
        boolean published = false;
        try {
            final File destination = new File(staging, safeName(queryDisplayName(rootDocument)));
            copyDocumentTree(tree, rootDocument, destination);
            published = completeImports(request, staging, new String[]{destination.getAbsolutePath()});
        } finally {
            if (!published) discardImportStaging(staging);
        }
    }

    private boolean completeImports(PendingRequest request, File root, String[] paths)
            throws IOException {
        synchronized (requestLock) {
            synchronized (request) {
                // 遅いプロバイダーは、ネイティブ側の取り消し・中断の後に完了する場合がある。
                if (shuttingDown || pendingRequest != request || request.done.getCount() == 0) return false;
                importLeases.register(root, paths);
                request.result = paths;
                request.done.countDown();
                return true;
            }
        }
    }

    private void discardImportStaging(File root) {
        try { deleteImportStaging(root); } catch (IOException ignored) {
            // 整理処理で、元のプロバイダー・取り消しのエラーを置き換えない。
        }
    }

    private void deleteImportStaging(File root) throws IOException {
        final File cache = new File(activity.getCacheDir(), "cardwirth-saf").getCanonicalFile();
        final File absolute = root.getAbsoluteFile();
        if (!absolute.equals(root.getCanonicalFile()) || !cache.equals(absolute.getParentFile()) ||
                !(absolute.getName().startsWith("upload-files-") ||
                  absolute.getName().startsWith("upload-folder-")))
            throw new IOException("Refusing to release an unowned import directory");
        deleteImportTree(absolute);
    }

    private static void deleteImportTree(File path) throws IOException {
        // 後の利用側が準備領域に置いたシンボリックリンクをたどらない。
        if (path.getAbsoluteFile().equals(path.getCanonicalFile())) {
            final File[] children = path.listFiles();
            if (children != null) for (File child : children) deleteImportTree(child);
        }
        if (!path.delete() && path.exists()) throw new IOException("Cannot release import staging");
    }

    private void finishExportTarget(PendingRequest request, Intent data) throws IOException {
        final Uri target = data.getData();
        if (target == null) {
            throw new IOException("書き出し先が選択されていません");
        }
        takePermission(target, data.getFlags(), true);
        final Uri exportTarget;
        if (request.kind == PICK_SAVE_FILE) {
            exportTarget = target;
        } else {
            final String rootId = DocumentsContract.getTreeDocumentId(target);
            exportTarget = DocumentsContract.buildDocumentUriUsingTree(target, rootId);
        }
        // 書き出しのバックアップは、OS のキャッシュ削除とアプリ終了を越えて保持する。
        final File root = new File(activity.getFilesDir(),
            "CardWirthRecovery/" + java.util.UUID.randomUUID().toString());
        if (!root.mkdirs()) throw new IOException("書き出しの退避領域を作成できません");
        boolean published = false;
        try {
            final String name = request.kind == PICK_DOWNLOAD_FOLDER
                ? "contents" : request.suggestedName;
            final File staging = new File(root, safeName(name));
            final boolean exactFile = request.kind == PICK_SAVE_FILE;
            if (!exactFile && !staging.mkdirs()) {
                throw new IOException("一時フォルダを作成できません");
            }
            synchronized (requestLock) {
                synchronized (request) {
                    // 公開と終了はロックを共有する。利用権が終了時の整理から
                    // 見える状態にするか、公開を行わないかのどちらかにする。
                    if (shuttingDown || pendingRequest != request || request.done.getCount() == 0) return;
                    pendingExports.put(staging.getAbsolutePath(), new PendingExport(
                        exportTarget, exactFile, request.kind == PICK_SAVE_NAMED_FOLDER));
                    request.result = new String[]{staging.getAbsolutePath()};
                    published = true;
                    request.done.countDown();
                }
            }
        } finally {
            // 破棄してよいのは、この新しい未公開のルートだけ。既存の recoveryRequired
            // の書き出しは、終了時も意図的に保持する。
            if (!published) deleteTree(root);
        }
    }

    private List<Uri> selectedUris(Intent data) {
        final ArrayList<Uri> result = new ArrayList<>();
        final ClipData clip = data.getClipData();
        if (clip != null) {
            for (int index = 0; index < clip.getItemCount(); ++index) {
                final Uri uri = clip.getItemAt(index).getUri();
                if (uri != null) {
                    result.add(uri);
                }
            }
        } else if (data.getData() != null) {
            result.add(data.getData());
        }
        return result;
    }

    private void takePermission(Uri uri, int resultFlags, boolean writable) {
        int flags = resultFlags & (Intent.FLAG_GRANT_READ_URI_PERMISSION |
                                   Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        flags |= Intent.FLAG_GRANT_READ_URI_PERMISSION;
        if (writable) {
            flags |= Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
        }
        try {
            activity.getContentResolver().takePersistableUriPermission(uri, flags);
        } catch (SecurityException ignored) {
            // プロバイダーによっては、現在の Activity の結果に対してだけアクセス権を与える。
        }
    }

    private File newStagingDirectory(String prefix) throws IOException {
        final File parent = new File(activity.getCacheDir(), "cardwirth-saf");
        if (!parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("一時領域を作成できません");
        }
        final File result = new File(parent, prefix + "-" + System.nanoTime());
        if (!result.mkdirs()) {
            throw new IOException("一時領域を作成できません");
        }
        return result.getCanonicalFile();
    }

    private String queryDisplayName(Uri uri) {
        final ContentResolver resolver = activity.getContentResolver();
        try (Cursor cursor = resolver.query(uri,
                new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0)) {
                return safeName(cursor.getString(0));
            }
        } catch (RuntimeException ignored) {
            // OpenableColumns のないプロバイダーには、下の URI の末尾を使う。
        }
        final String tail = uri.getLastPathSegment();
        return safeName(tail == null ? "document" : tail);
    }

    private void copyDocumentTree(Uri tree, Uri document, File destination) throws IOException {
        final String documentId = DocumentsContract.getDocumentId(document);
        final Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, documentId);
        if (!destination.isDirectory() && !destination.mkdirs()) {
            throw new IOException("フォルダを作成できません: " + destination.getName());
        }
        final ContentResolver resolver = activity.getContentResolver();
        final String[] columns = {
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE
        };
        try (Cursor cursor = resolver.query(children, columns, null, null, null)) {
            if (cursor == null) {
                throw new IOException("フォルダ内容を取得できません");
            }
            while (cursor.moveToNext()) {
                final String childId = cursor.getString(0);
                final String name = safeName(cursor.getString(1));
                final String mime = cursor.getString(2);
                final Uri child = DocumentsContract.buildDocumentUriUsingTree(tree, childId);
                final File childFile = new File(destination, uniqueName(destination, name));
                if (DIRECTORY_MIME.equals(mime)) {
                    copyDocumentTree(tree, child, childFile);
                } else {
                    copyUriToFile(child, childFile);
                }
            }
        }
    }

    private void copyUriToFile(Uri source, File destination) throws IOException {
        final File parent = destination.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("一時フォルダを作成できません");
        }
        try (InputStream raw = activity.getContentResolver().openInputStream(source)) {
            if (raw == null) {
                throw new IOException("ファイルを開けません: " + source);
            }
            try (InputStream input = new BufferedInputStream(raw);
                 OutputStream output = new BufferedOutputStream(new FileOutputStream(destination))) {
                copy(input, output);
            }
        }
    }

    private void copyFileToUri(File source, Uri destination) throws IOException {
        if (!source.isFile()) {
            throw new IOException("書き出すファイルがありません: " + source.getName());
        }
        try (InputStream input = new BufferedInputStream(new FileInputStream(source));
             OutputStream raw = activity.getContentResolver().openOutputStream(destination, "wt")) {
            if (raw == null) {
                throw new IOException("書き出し先を開けません");
            }
            try (OutputStream output = new BufferedOutputStream(raw)) {
                copy(input, output);
            }
        }
    }

    private void copyDirectoryContents(File source, Uri destinationTree) throws IOException {
        copyDirectoryContents(source, destinationTree, null);
    }

    private void copyDirectoryContents(File source, Uri destinationTree,
                                       List<String> changedNames) throws IOException {
        final File[] children = source.listFiles();
        if (children == null) {
            throw new IOException("書き出すフォルダを読み込めません");
        }
        for (File child : children) {
            // 最初の破壊的操作の直前に記録する。置換が失敗しても、以
            // 前のプロバイダー文書はすでに削除されている場合がある。
            if (changedNames != null) changedNames.add(child.getName());
            if (child.isDirectory()) {
                final Uri directory = replaceChild(destinationTree, child.getName(), DIRECTORY_MIME);
                copyDirectoryContents(child, directory);
            } else {
                final Uri document = replaceChild(
                    destinationTree, child.getName(), mimeForName(child.getName()));
                copyFileToUri(child, document);
            }
        }
    }

    private Uri replaceChild(Uri parentTree, String name, String mime) throws IOException {
        final ContentResolver resolver = activity.getContentResolver();
        final String parentId = DocumentsContract.getDocumentId(parentTree);
        final Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(parentTree, parentId);
        final String[] columns = {
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME
        };
        try (Cursor cursor = resolver.query(children, columns, null, null, null)) {
            if (cursor != null) {
                while (cursor.moveToNext()) {
                    if (name.equals(cursor.getString(1))) {
                        final Uri existing = DocumentsContract.buildDocumentUriUsingTree(
                            parentTree, cursor.getString(0));
                        if (!DocumentsContract.deleteDocument(resolver, existing)) {
                            throw new IOException("既存項目を置き換えられません: " + name);
                        }
                        break;
                    }
                }
            }
        }
        final Uri created = DocumentsContract.createDocument(resolver, parentTree, mime, name);
        if (created == null) {
            throw new IOException("項目を作成できません: " + name);
        }
        return created;
    }

    private static void copy(InputStream input, OutputStream output) throws IOException {
        final byte[] buffer = new byte[64 * 1024];
        int count;
        while ((count = input.read(buffer)) >= 0) {
            output.write(buffer, 0, count);
        }
        output.flush();
    }

    private static String uniqueName(File parent, String requested) {
        final String safe = safeName(requested);
        if (!new File(parent, safe).exists()) {
            return safe;
        }
        final int dot = safe.lastIndexOf('.');
        final String base = dot > 0 ? safe.substring(0, dot) : safe;
        final String extension = dot > 0 ? safe.substring(dot) : "";
        for (int suffix = 2; suffix < 10000; ++suffix) {
            final String candidate = base + " (" + suffix + ")" + extension;
            if (!new File(parent, candidate).exists()) {
                return candidate;
            }
        }
        return base + "-" + System.nanoTime() + extension;
    }

    private static String safeName(String name) {
        if (name == null || name.isEmpty()) {
            return "document";
        }
        final String safe = name.replace('/', '_').replace('\\', '_').replace('\u0000', '_');
        return safe.equals(".") || safe.equals("..") ? "document" : safe;
    }

    private static String mimeForName(String name) {
        final String guessed = URLConnection.guessContentTypeFromName(name);
        return guessed == null ? "application/octet-stream" : guessed;
    }

    private static String message(Exception error) {
        final String value = error.getMessage();
        String result = value == null || value.isEmpty() ? error.getClass().getSimpleName() : value;
        for (Throwable suppressed : error.getSuppressed())
            result += "\n復元失敗: " + (suppressed instanceof Exception
                ? message((Exception)suppressed) : suppressed.toString());
        return result;
    }

    private static void deleteTree(File path) {
        if (path == null || !path.exists()) {
            return;
        }
        final File[] children = path.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteTree(child);
            }
        }
        // 可能な範囲で整理する。Android 自身のキャッシュ領域は、最終的には OS が消去する。
        path.delete();
    }

    private void completeEmptySelection(PendingRequest request) {
        synchronized (requestLock) {
            synchronized (request) {
                if (shuttingDown || pendingRequest != request || request.done.getCount() == 0) return;
                request.result = new String[0];
                request.done.countDown();
            }
        }
    }

    private void completeFailure(PendingRequest request, String error) {
        synchronized (request) {
            if (request.done.getCount() == 0) return;
            request.error = error;
            request.result = null;
            request.done.countDown();
        }
    }

    private static final class PendingRequest {
        final int kind;
        final String suggestedName;
        final boolean multiple;
        final String[] mimeTypes;
        final CountDownLatch done = new CountDownLatch(1);
        volatile String[] result;
        volatile String error = "";

        PendingRequest(int kind, String suggestedName, boolean multiple, String[] mimeTypes) {
            this.kind = kind;
            this.suggestedName = suggestedName;
            this.multiple = multiple;
            this.mimeTypes = mimeTypes;
        }
    }

    private static final class PendingExport {
        final Uri target;
        final boolean exactFile;
        final boolean createNamedDirectory;
        boolean recoveryRequired;

        PendingExport(Uri target, boolean exactFile, boolean createNamedDirectory) {
            this.target = target;
            this.exactFile = exactFile;
            this.createNamedDirectory = createNamedDirectory;
        }
    }

}
