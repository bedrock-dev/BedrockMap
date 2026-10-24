package org.bedrockmap;

import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

public final class WorldDirectoryImport {
    private WorldDirectoryImport() {}

    /**
     * Resolves a SAF tree URI from the system external-storage provider to a
     * real filesystem path. Other providers (cloud, downloads) deliberately
     * return an empty string because they have no stable native path.
     */
    @SuppressWarnings("deprecation")
    public static String resolveTreePath(Context context, String source) {
        try {
            Uri tree = Uri.parse(source);
            if (!"com.android.externalstorage.documents".equals(tree.getAuthority())) return "";
            String documentId = DocumentsContract.getTreeDocumentId(tree);
            int separator = documentId.indexOf(':');
            if (separator < 0) return "";

            String volume = documentId.substring(0, separator);
            String relative = documentId.substring(separator + 1);
            File root;
            if ("primary".equalsIgnoreCase(volume)) {
                root = Environment.getExternalStorageDirectory();
            } else {
                root = new File("/storage/" + volume);
            }

            String canonicalRoot = root.getCanonicalPath();
            File resolved = relative.isEmpty() ? root : new File(root, relative);
            String canonicalPath = resolved.getCanonicalPath();
            if (!canonicalPath.equals(canonicalRoot)
                    && !canonicalPath.startsWith(canonicalRoot + File.separator)) {
                return "";
            }
            return resolved.isDirectory() ? canonicalPath : "";
        } catch (Exception exception) {
            return "";
        }
    }

    // Called on a worker thread while the folder picker's URI grant is active.
    public static String copyWorld(Context context, String source, String destination) {
        try {
            Uri tree = Uri.parse(source);
            String rootId = DocumentsContract.getTreeDocumentId(tree);
            validateWorld(context, tree, rootId);
            copyDirectory(context, tree, rootId, new File(destination), 0);
            File root = new File(destination);
            if (!new File(root, "level.dat").isFile() || !new File(root, "db").isDirectory()) {
                return "The selected directory must contain level.dat and db.";
            }
            return "";
        } catch (Exception exception) {
            String message = exception.getMessage();
            return message != null ? message : exception.getClass().getSimpleName();
        }
    }

    private static void validateWorld(Context context, Uri tree, String rootId) throws IOException {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, rootId);
        String[] columns = {Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE};
        boolean levelDat = false;
        boolean database = false;
        try (Cursor cursor = context.getContentResolver().query(children, columns, null, null, null)) {
            if (cursor == null) throw new IOException("Cannot list the selected directory.");
            while (cursor.moveToNext()) {
                String name = cursor.getString(0);
                boolean directory = Document.MIME_TYPE_DIR.equals(cursor.getString(1));
                if ("level.dat".equals(name) && !directory) levelDat = true;
                if ("db".equals(name) && directory) database = true;
            }
        }
        if (!levelDat || !database) {
            throw new IOException("The selected directory must contain level.dat and db.");
        }
    }

    private static void copyDirectory(Context context, Uri tree, String documentId,
                                      File destination, int depth) throws IOException {
        if (depth > 64) throw new IOException("Directory nesting is too deep.");
        if (!destination.isDirectory() && !destination.mkdirs()) {
            throw new IOException("Cannot create import directory.");
        }
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, documentId);
        String[] columns = {Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME,
                            Document.COLUMN_MIME_TYPE};
        try (Cursor cursor = context.getContentResolver().query(children, columns, null, null, null)) {
            if (cursor == null) throw new IOException("Cannot list the selected directory.");
            while (cursor.moveToNext()) {
                String id = cursor.getString(0);
                String name = cursor.getString(1);
                String mime = cursor.getString(2);
                if (name == null || name.isEmpty() || name.equals(".") || name.equals("..")
                        || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0) {
                    throw new IOException("Invalid document name.");
                }
                File target = new File(destination, name);
                if (target.exists()) throw new IOException("Duplicate document name: " + name);
                if (Document.MIME_TYPE_DIR.equals(mime)) {
                    copyDirectory(context, tree, id, target, depth + 1);
                } else {
                    // LevelDB creates its own process-local database lock.
                    if (depth == 1 && destination.getName().equals("db") && name.equals("LOCK")) continue;
                    Uri document = DocumentsContract.buildDocumentUriUsingTree(tree, id);
                    try (InputStream input = context.getContentResolver().openInputStream(document);
                         FileOutputStream output = new FileOutputStream(target)) {
                        if (input == null) throw new IOException("Cannot read " + name);
                        byte[] buffer = new byte[256 * 1024];
                        int count;
                        while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
                    }
                }
            }
        }
    }
}
