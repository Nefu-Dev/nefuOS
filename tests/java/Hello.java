// Java test case #1 for the nefuOS mini JVM (core/apps/minijvm.cpp).
// Compiled with the host JDK (javac), embedded into the headless test as a
// byte array, then interpreted by minijvm. Covers: string constants, int and
// long arithmetic, for/if/else, iinc, static fields + <clinit>, same-class
// static methods, recursion, int arrays + arraylength, Math.abs/min/max,
// String.length/charAt, System.out.print/println.
// NOTE: string concatenation uses invokedynamic on modern javac and is
// intentionally not part of the minijvm subset (see jdk-integration-plan.md).
public class Hello {
    static int counter = 0;

    public static int add(int a, int b) {
        counter = counter + a + b;
        return a + b;
    }

    public static int fib(int n) {
        if (n < 2) return n;
        return fib(n - 1) + fib(n - 2);
    }

    public static void main(String[] args) {
        System.out.println("Hello, nefuOS from Java!");
        System.out.println(6 * 7);
        long big = 1234567890123L;
        System.out.println(big + 1L);
        int sum = 0;
        for (int i = 1; i <= 10; i++) sum += i;
        System.out.println(sum);
        System.out.println(add(20, 22));
        System.out.println(counter);
        System.out.println(fib(10));
        int[] arr = {5, 9, 2, 7, 4};
        int max = arr[0];
        for (int i = 1; i < arr.length; i++) {
            if (arr[i] > max) max = arr[i];
        }
        System.out.println(max);
        System.out.println(Math.abs(-42));
        System.out.println(Math.max(3, 8));
        System.out.println(Math.min(9, 4));
        String s = "nefuOS";
        System.out.println(s.length());
        System.out.println(s.charAt(3));
        if (sum == 55) System.out.println(1000);
        else System.out.println(-1000);
        System.out.print("joined");
        System.out.print(123);
        System.out.println();
    }
}
