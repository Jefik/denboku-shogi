package jp.denboku;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTrack;
import android.os.Build;
import java.io.File;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.util.HashMap;
import java.util.Map;

/* The game's sounds: every WAV of the set in use decoded once into a static AudioTrack in
 * low-latency mode, which Android plays through its fast mixer. SoundPool's tracks go through the
 * normal one, with its larger buffers, so a sound was heard well after the move. */
final class Sounds {
    private final Map<String, AudioTrack> tracks = new HashMap<>();
    private String set; /* the folder whose sounds are loaded */

    /* "FILE VOLUME", volume 0-100. */
    synchronized void play(String args) {
        int sp = args.lastIndexOf(' ');
        String file = args.substring(0, sp);
        float v = Integer.parseInt(args.substring(sp + 1)) / 100f;
        String dir = new File(file).getParent();
        if (dir != null && !dir.equals(set)) {
            release();
            set = dir;
            File[] all = new File(dir).listFiles((d, n) -> n.endsWith(".wav"));
            for (File f : all != null ? all : new File[0])
                load(f.getPath());
        }
        if (!tracks.containsKey(file))
            load(file); /* one a set lacks, from the sounds folder itself */
        AudioTrack t = tracks.get(file);
        if (t == null)
            return;
        try {
            t.stop();
            t.reloadStaticData();
            t.setVolume(v);
            t.play();
        } catch (IllegalStateException e) {
            /* a track the system took back: nothing to play */
        }
    }

    private void load(String path) {
        AudioTrack t = null;
        try {
            ByteBuffer b = ByteBuffer.wrap(Files.readAllBytes(new File(path).toPath())).order(ByteOrder.LITTLE_ENDIAN);
            int channels = 0, rate = 0, bits = 0, at = 12;
            while (at + 8 <= b.limit()) {
                int size = b.getInt(at + 4);
                String id = new String(b.array(), at, 4, java.nio.charset.StandardCharsets.US_ASCII);
                if (id.equals("fmt ")) {
                    channels = b.getShort(at + 10);
                    rate = b.getInt(at + 12);
                    bits = b.getShort(at + 22);
                } else if (id.equals("data") && bits == 16 && channels > 0) {
                    int len = Math.min(size, b.limit() - at - 8) & ~1;
                    AudioTrack.Builder tb = new AudioTrack.Builder()
                        .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
                                                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build())
                        .setAudioFormat(new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT).setSampleRate(rate)
                                            .setChannelMask(channels == 1 ? AudioFormat.CHANNEL_OUT_MONO : AudioFormat.CHANNEL_OUT_STEREO).build())
                        .setTransferMode(AudioTrack.MODE_STATIC)
                        .setBufferSizeInBytes(len);
                    if (Build.VERSION.SDK_INT >= 26)
                        tb.setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY);
                    t = tb.build();
                    t.write(b.array(), at + 8, len);
                    tracks.put(path, t);
                    return;
                }
                at += 8 + size + (size & 1);
            }
        } catch (IOException | RuntimeException e) {
            if (t != null)
                t.release();
        }
    }

    synchronized void release() {
        for (AudioTrack t : tracks.values())
            t.release();
        tracks.clear();
        set = null;
    }
}
