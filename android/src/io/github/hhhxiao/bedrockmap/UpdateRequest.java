package io.github.hhhxiao.bedrockmap;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import javax.net.ssl.HttpsURLConnection;

/** System HTTPS transport, called only from the update checker's worker. */
public final class UpdateRequest {
    private UpdateRequest() {}

    public static String fetch(String url, String userAgent) {
        HttpsURLConnection connection = null;
        try {
            connection = (HttpsURLConnection) new URL(url).openConnection();
            connection.setConnectTimeout(10000);
            connection.setReadTimeout(10000);
            connection.setRequestProperty("User-Agent", userAgent);
            connection.setRequestProperty("Accept", "application/vnd.github+json");
            int status = connection.getResponseCode();
            if (status != HttpsURLConnection.HTTP_OK) {
                throw new IOException("GitHub HTTP " + status);
            }
            try (InputStream input = connection.getInputStream();
                 ByteArrayOutputStream output = new ByteArrayOutputStream()) {
                byte[] buffer = new byte[8192];
                int count;
                while ((count = input.read(buffer)) != -1) {
                    output.write(buffer, 0, count);
                }
                return new String(output.toByteArray(), StandardCharsets.UTF_8);
            }
        } catch (Exception error) {
            AppLog.w("UpdateChecker", "Update check failed", error);
            return "";
        } finally {
            if (connection != null) connection.disconnect();
        }
    }
}
