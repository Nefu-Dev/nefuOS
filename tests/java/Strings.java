// Java test case #2: String methods + new String() on the mini JVM.
public class Strings {
    public static void main(String[] args) {
        String a = "nefuOS";
        String b = "nefuOS";
        String c = "nefu";
        System.out.println(a.equals(b));
        System.out.println(a.equals(c));
        System.out.println(a.substring(4));
        System.out.println(a.substring(0, 4));
        String d = new String();
        System.out.println(d.length());
        System.out.print('A');
        System.out.println(1);
        boolean t = true;
        System.out.println(t);
    }
}
