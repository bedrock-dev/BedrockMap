package io.github.hhhxiao.bedrockmap;

import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.HashMap;
import java.util.Map;

public final class SafTreeIo {
    private SafTreeIo() {}

    /**
     * Resolves a SAF tree URI from the system external-storage provider to a
     * real filesystem path. Other providers (cloud, downloads) deliberately
     * return an empty string because they have no stable native path.
     */
    @SuppressWarnings("deprecation")
    public static String resolveTreePath(Context context, String source) {
        try {
            Uri tree = DocumentMetadata.grantedUri(context, source);
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
    public static String copyTree(Context context, String source, String destination) {
        try {
            Uri tree = DocumentMetadata.grantedUri(context, source);
            String rootId = DocumentsContract.getTreeDocumentId(tree);
            copyDirectory(context, tree, rootId, new File(destination), 0);
            return "";
        } catch (Exception exception) {
            String message = exception.getMessage();
            return message != null ? message : exception.getClass().getSimpleName();
        }
    }

    // Called on a worker thread while the selected tree grant is active.
    public static boolean isTreeEmpty(Context context, String source) {
        try {
            Uri tree = DocumentMetadata.grantedUri(context, source);
            String rootId = DocumentsContract.getTreeDocumentId(tree);
            return listChildren(context, tree, rootId).isEmpty();
        } catch (Exception exception) {
            return false;
        }
    }

    // Copies the world directory directly into the selected folder.
    public static String copyLocalTree(Context context, String source, String destination) {
        try {
            File sourceDirectory = new File(source);
            if (!sourceDirectory.isDirectory()) throw new IOException("The source world directory is unavailable.");
            Uri tree = DocumentMetadata.grantedUri(context, destination);
            String rootId = DocumentsContract.getTreeDocumentId(tree);
            copyLocalDirectory(context, tree, rootId, sourceDirectory, 0, false);
            return "";
        } catch (Exception exception) {
            String message = exception.getMessage();
            return message != null ? message : exception.getClass().getSimpleName();
        }
    }

    private static void copyDirectory(Context context, Uri tree, String documentId,
                                      File destination, int depth) throws IOException {
        if (depth > 64) throw new IOException("Directory nesting is too deep.");
        if (!destination.isDirectory() && !destination.mkdirs()) {
            throw new IOException("Cannot create destination directory.");
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

    private static final class ChildDocument {
        final String documentId;
        final boolean directory;

        ChildDocument(String documentId, boolean directory) {
            this.documentId = documentId;
            this.directory = directory;
        }
    }

    private static Map<String, ChildDocument> listChildren(Context context, Uri tree, String parentId)
            throws IOException {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId);
        String[] columns = {Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME,
                            Document.COLUMN_MIME_TYPE};
        Map<String, ChildDocument> result = new HashMap<>();
        try (Cursor cursor = context.getContentResolver().query(children, columns, null, null, null)) {
            if (cursor == null) throw new IOException("Cannot list the selected directory.");
            while (cursor.moveToNext()) {
                String id = cursor.getString(0);
                String name = cursor.getString(1);
                String mime = cursor.getString(2);
                if (id == null || name == null || name.isEmpty()) continue;
                if (result.put(name, new ChildDocument(id, Document.MIME_TYPE_DIR.equals(mime))) != null)
                    throw new IOException("Duplicate document name: " + name);
            }
        }
        return result;
    }

    private static boolean validName(String name) {
        return !name.isEmpty() && !name.equals(".") && !name.equals("..")
                && name.indexOf('/') < 0 && name.indexOf('\\') < 0 && name.indexOf(':') < 0
                && name.indexOf('\0') < 0 && !name.endsWith(".") && !name.endsWith(" ");
    }

    private static Uri ensureChild(Context context, Uri tree, String parentId,
                                   Map<String, ChildDocument> children, String name, boolean directory)
            throws IOException {
        ChildDocument existing = children.get(name);
        if (existing != null) {
            if (existing.directory != directory)
                throw new IOException("Cannot replace a destination entry of a different type: " + name);
            return DocumentsContract.buildDocumentUriUsingTree(tree, existing.documentId);
        }
        Uri parent = DocumentsContract.buildDocumentUriUsingTree(tree, parentId);
        String mime = directory ? Document.MIME_TYPE_DIR : "application/octet-stream";
        Uri created = DocumentsContract.createDocument(context.getContentResolver(), parent, mime, name);
        if (created == null) throw new IOException("Cannot create destination entry: " + name);
        return created;
    }

    private static void copyLocalDirectory(Context context, Uri tree, String parentId, File source,
                                           int depth, boolean inDatabaseDirectory) throws IOException {
        if (depth > 64) throw new IOException("Directory nesting is too deep.");
        File[] entries = source.listFiles();
        if (entries == null) throw new IOException("Cannot list the source world directory.");
        Map<String, ChildDocument> children = listChildren(context, tree, parentId);
        for (File entry : entries) {
            String name = entry.getName();
            if (!validName(name)) throw new IOException("Unsupported source file name: " + name);
            if (inDatabaseDirectory
                    && (name.equals("LOCK") || name.equals("LOG") || name.equals("LOG.old"))) {
                continue;
            }
            if (entry.isDirectory()) {
                Uri directory = ensureChild(context, tree, parentId, children, name, true);
                String documentId = DocumentsContract.getDocumentId(directory);
                copyLocalDirectory(context, tree, documentId, entry, depth + 1,
                                   inDatabaseDirectory || name.equals("db"));
                continue;
            }
            if (!entry.isFile()) throw new IOException("Unsupported source file: " + name);
            Uri document = ensureChild(context, tree, parentId, children, name, false);
            try (InputStream input = new FileInputStream(entry);
                 OutputStream output = context.getContentResolver().openOutputStream(document, "wt")) {
                if (output == null) throw new IOException("Cannot write " + name);
                byte[] buffer = new byte[256 * 1024];
                int count;
                while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
                output.flush();
            }
        }
    }
}
