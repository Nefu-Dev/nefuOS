// Java test case #4: exception machinery - try/catch/finally, custom
// exceptions, NPE/AIOOBE/ArithmeticException, continued execution after catch.
public class Exceptions {
    static class MyException extends RuntimeException {
        MyException(String m) { super(m); }
    }

    public static void main(String[] args) {
        try {
            int x = 10 / 0;
        } catch (ArithmeticException e) {
            System.out.println("caught-div0");
        }
        try {
            System.out.println("try1");
        } finally {
            System.out.println("fin1");
        }
        try {
            int[] a = new int[3];
            System.out.println(a[5]);
        } catch (ArrayIndexOutOfBoundsException e) {
            System.out.println("oob");
        } finally {
            System.out.println("fin2");
        }
        try {
            throw new MyException("boom");
        } catch (MyException e) {
            System.out.println(e.getMessage());
        }
        try {
            throw new RuntimeException("rt");
        } catch (RuntimeException e) {
            System.out.println(e.getMessage());
        }
        try {
            String s = null;
            System.out.println(s.length());
        } catch (NullPointerException e) {
            System.out.println("npe");
        }
        System.out.println("done");
    }
}
