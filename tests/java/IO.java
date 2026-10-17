// Java test case #6: java/io over memory (ByteArray streams) + File metadata.
// Real file streams (FileInputStream/FileOutputStream) are exercised by the
// host end-to-end run where the terminal registers the VFS hooks.
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;

public class IO {
    public static void main(String[] args) {
        ByteArrayOutputStream bos = new ByteArrayOutputStream();
        bos.write(65);
        bos.write(66);
        bos.write(67);
        byte[] data = bos.toByteArray();
        System.out.println(data.length);
        ByteArrayInputStream bis = new ByteArrayInputStream(data);
        System.out.println(bis.read());
        System.out.println(bis.read());
        System.out.println(bis.read());
        System.out.println(bis.read());
        File f = new File("/nonexistent.txt");
        System.out.println(f.exists());
        System.out.println(f.isFile());
        System.out.println(f.getName());
        System.out.println("done-io");
    }
}
