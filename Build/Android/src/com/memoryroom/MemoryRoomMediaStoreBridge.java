package com.memoryroom;

import android.app.Activity;
import android.content.ContentResolver;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Matrix;
import android.media.ExifInterface;
import android.net.Uri;
import android.provider.MediaStore;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.InputStream;

public class MemoryRoomMediaStoreBridge {
    private static Activity activity() { return com.epicgames.unreal.GameActivity.Get(); }

    // Lists folders containing images, including parent folders with recursive counts.
    public static String queryAlbumsJson() {
        JSONObject root = new JSONObject();
        try {
            Activity a = activity();
            if (a == null) {
                throw new IllegalStateException("GameActivity is null");
            }

            ContentResolver cr = a.getContentResolver();
            String[] projection = new String[] { MediaStore.Images.Media.RELATIVE_PATH };
            final java.util.HashMap<String, Integer> counts = new java.util.HashMap<>();
            try (Cursor cursor = cr.query(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, projection, null, null, null)) {
                while (cursor != null && cursor.moveToNext()) {
                    if (cursor.isNull(0)) continue;
                    String path = cursor.getString(0);
                    if (path == null || path.length() == 0) continue;
                    if (!path.endsWith("/")) path += "/";
                    String current = path;
                    while (current.length() > 0) {
                        Integer c = counts.get(current);
                        counts.put(current, c == null ? 1 : c + 1);
                        int end = current.length() - 1;
                        int slash = current.lastIndexOf('/', end - 1);
                        if (slash < 0) break;
                        current = current.substring(0, slash + 1);
                    }
                }
            }
            java.util.ArrayList<String> paths = new java.util.ArrayList<>(counts.keySet());
            java.util.Collections.sort(paths, new java.util.Comparator<String>() {
                @Override public int compare(String x, String y) {
                    int byCount = counts.get(y) - counts.get(x);
                    return byCount != 0 ? byCount : x.compareToIgnoreCase(y);
                }
            });
            JSONArray albums = new JSONArray();
            for (String path : paths) {
                String trimmed = path.substring(0, path.length() - 1);
                int slash = trimmed.lastIndexOf('/');
                String name = trimmed.substring(slash + 1);
                String parentPath = slash < 0 ? "" : trimmed.substring(0, slash + 1);
                JSONObject o = new JSONObject();
                o.put("id", "@path:" + path);
                o.put("name", name);
                o.put("parentId", parentPath.length() == 0 ? "@root" : "@path:" + parentPath);
                o.put("count", counts.get(path));
                albums.put(o);
            }
            root.put("success", true); root.put("albums", albums);
        } catch (Throwable t) {
            try { root.put("success", false); root.put("error", t.toString()); root.put("albums", new JSONArray()); } catch (Throwable ignored) {}
        }
        return root.toString();
    }

    // Accepts legacy numeric bucket IDs or '@path:' relative paths. Paths include descendants.
    public static String queryImagesJson(int offset, int limit, String bucketId) {
        JSONObject root = new JSONObject();
        try {
            Activity a = activity();
            if (a == null) {
                throw new IllegalStateException("GameActivity is null");
            }

            ContentResolver cr = a.getContentResolver();
            Uri collection = MediaStore.Images.Media.EXTERNAL_CONTENT_URI;
            String[] projection = new String[] {
                MediaStore.Images.Media._ID, MediaStore.Images.Media.DISPLAY_NAME,
                MediaStore.Images.Media.WIDTH, MediaStore.Images.Media.HEIGHT,
                MediaStore.Images.Media.ORIENTATION, MediaStore.Images.Media.MIME_TYPE,
                MediaStore.Images.Media.DATE_TAKEN, MediaStore.Images.Media.DATE_MODIFIED
            };
            String sort = MediaStore.Images.Media.DATE_MODIFIED + " DESC";
            boolean filtered = bucketId != null && bucketId.length() > 0 && !"@all".equals(bucketId);
            String selection = null;
            String[] selectionArgs = null;
            if (filtered && bucketId.startsWith("@path:")) {
                String path = bucketId.substring("@path:".length());
                if (!path.endsWith("/")) path += "/";
                path = path.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_");
                selection = MediaStore.Images.Media.RELATIVE_PATH + " LIKE ? ESCAPE '\\'";
                selectionArgs = new String[] { path + "%" };
            } else if (filtered) {
                selection = MediaStore.Images.Media.BUCKET_ID + " = ?";
                selectionArgs = new String[] { bucketId };
            }
            try (Cursor cursor = cr.query(collection, projection, selection, selectionArgs, sort)) {
                int total = cursor == null ? 0 : cursor.getCount();
                JSONArray items = new JSONArray();
                if (cursor != null && cursor.moveToPosition(Math.max(0, offset))) {
                    int emitted = 0;
                    int safeLimit = Math.max(1, limit);
                    do {
                        long id = cursor.getLong(0);
                        String name = cursor.getString(1);
                        int width = cursor.isNull(2) ? 0 : cursor.getInt(2);
                        int height = cursor.isNull(3) ? 0 : cursor.getInt(3);
                        int orientation = cursor.isNull(4) ? 0 : cursor.getInt(4);
                        if (orientation == 90 || orientation == 270) { int t = width; width = height; height = t; }
                        JSONObject o = new JSONObject();
                        o.put("id", Long.toString(id));
                        o.put("name", name == null ? "" : name);
                        o.put("uri", Uri.withAppendedPath(collection, Long.toString(id)).toString());
                        o.put("width", width); o.put("height", height); o.put("orientation", orientation);
                        String mime = cursor.getString(5);
                        o.put("mime", mime == null ? "" : mime);
                        o.put("dateTaken", cursor.isNull(6) ? 0 : cursor.getLong(6));
                        o.put("dateModified", cursor.isNull(7) ? 0 : cursor.getLong(7));
                        items.put(o);
                        emitted++;
                    } while (emitted < safeLimit && cursor.moveToNext());
                }
                root.put("success", true); root.put("total", total); root.put("items", items);
            }
        } catch (Throwable t) {
            try { root.put("success", false); root.put("total", 0); root.put("error", t.toString()); root.put("items", new JSONArray()); } catch (Throwable ignored) {}
        }
        return root.toString();
    }

    public static int[] loadImageArgb(String uriString, int maxDim) {
        try {
            Activity a = activity();
            if (a == null || uriString == null || uriString.length() == 0) {
                return null;
            }

            ContentResolver cr = a.getContentResolver();
            Uri uri = Uri.parse(uriString);
            BitmapFactory.Options bounds = new BitmapFactory.Options();
            bounds.inJustDecodeBounds = true;
            try (InputStream stream = cr.openInputStream(uri)) {
                if (stream == null) return null;
                BitmapFactory.decodeStream(stream, null, bounds);
            }

            int largest = Math.max(bounds.outWidth, bounds.outHeight);
            int sample = 1;
            while (largest / sample > Math.max(1, maxDim)) sample *= 2;

            BitmapFactory.Options opts = new BitmapFactory.Options();
            opts.inPreferredConfig = Bitmap.Config.ARGB_8888;
            opts.inSampleSize = sample;
            Bitmap bitmap;
            try (InputStream stream = cr.openInputStream(uri)) {
                if (stream == null) return null;
                bitmap = BitmapFactory.decodeStream(stream, null, opts);
            }
            if (bitmap == null) return null;

            int exif = 1;
            try (InputStream stream = cr.openInputStream(uri)) {
                if (stream != null) {
                    ExifInterface ei = new ExifInterface(stream);
                    exif = ei.getAttributeInt(ExifInterface.TAG_ORIENTATION, 1);
                }
            } catch (Throwable ignored) {}

            Matrix m = new Matrix();
            if (exif == ExifInterface.ORIENTATION_ROTATE_90) m.postRotate(90);
            else if (exif == ExifInterface.ORIENTATION_ROTATE_180) m.postRotate(180);
            else if (exif == ExifInterface.ORIENTATION_ROTATE_270) m.postRotate(270);
            if (!m.isIdentity()) {
                Bitmap rotated = Bitmap.createBitmap(bitmap, 0, 0, bitmap.getWidth(), bitmap.getHeight(), m, true);
                if (rotated != bitmap) bitmap.recycle();
                bitmap = rotated;
            }

            int w = bitmap.getWidth(), h = bitmap.getHeight();
            int[] out = new int[2 + w * h];
            out[0] = w; out[1] = h;
            bitmap.getPixels(out, 2, w, 0, 0, w, h);
            bitmap.recycle();
            return out;
        } catch (Throwable t) {
            return null;
        }
    }
}
