// Java test case #3: soft-float double/float arithmetic + java.lang.Math.
// Values are chosen so the real JRE and minijvm print the same short decimal
// (minijvm formats doubles with 6 fraction digits, see minijvm_core.cpp).
public class Maths {
    public static void main(String[] args) {
        float f = 1.5f;
        double d = 2.25;
        System.out.println(f);
        System.out.println(d);
        System.out.println(f * 2.0f);
        System.out.println(d / 1.5);
        System.out.println(f + d);
        System.out.println(d - f);
        System.out.println(Math.sqrt(2.25));
        System.out.println(Math.pow(2.0, 10.0));
        System.out.println(Math.abs(-3.5));
        System.out.println(Math.max(1.5, 2.5));
        System.out.println(Math.min(3.0, 2.0));
        System.out.println(Math.floor(2.7));
        System.out.println(Math.ceil(2.1));
        System.out.println(Math.round(2.5));
        System.out.println(Math.round(2.4));
        System.out.println(Math.round(Math.PI * 1000));
        System.out.println(Math.sin(0.0));
        System.out.println(Math.cos(0.0));
        System.out.println(Math.tan(0.0));
        System.out.println(Math.log(1.0));
        System.out.println(Math.round(Math.log10(1000.0) * 10));
        System.out.println(Math.exp(0.0));
        System.out.println(Math.round(Math.toRadians(180.0) * 1000));
        System.out.println(Math.toDegrees(Math.PI));
        double[] arr = {1.5, 2.5, 3.5};
        System.out.println(arr[1]);
        arr[2] = 4.5;
        System.out.println(arr[2]);
        System.out.println((int) 1.9);
        System.out.println((long) 2.9);
        System.out.println(f > 1.0f);
        System.out.println(d < 3.0);
        float[] fa = new float[2];
        fa[0] = 0.5f;
        fa[1] = 1.5f;
        System.out.println(fa[0] + fa[1]);
    }
}
