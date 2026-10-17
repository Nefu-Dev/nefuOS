// Java test case #5: JDK core library on the mini JVM - boxed primitives,
// StringBuilder, ArrayList/HashMap/HashSet/Iterator, Arrays, System.arraycopy.
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Arrays;

public class Collections {
    public static void main(String[] args) {
        Integer i = Integer.valueOf(42);
        System.out.println(i.intValue());
        int p = Integer.parseInt("123");
        System.out.println(p);
        long l = Long.parseLong("9876543210");
        System.out.println(l);
        Boolean b = Boolean.valueOf(true);
        System.out.println(b.booleanValue());
        Double dd = Double.valueOf(1.5);
        System.out.println(dd.doubleValue());
        System.out.println(Integer.toString(77));
        System.out.println(Long.toString(88L));

        StringBuilder sb = new StringBuilder();
        sb.append("A");
        sb.append(1);
        sb.append(2L);
        sb.append(1.5);
        sb.append(2.25f);
        sb.append('X');
        sb.append(true);
        System.out.println(sb.toString());

        ArrayList<String> list = new ArrayList<String>();
        list.add("one");
        list.add("two");
        list.add("three");
        System.out.println(list.size());
        System.out.println(list.get(1));
        list.set(1, "TWO");
        System.out.println(list.get(1));
        System.out.println(list.contains("two"));
        System.out.println(list.indexOf("three"));
        list.remove(2);
        System.out.println(list.size());

        HashMap<String, Integer> map = new HashMap<String, Integer>();
        map.put("a", Integer.valueOf(1));
        map.put("b", Integer.valueOf(2));
        System.out.println(map.size());
        System.out.println(map.get("a").intValue());
        System.out.println(map.containsKey("b"));
        map.remove("a");
        System.out.println(map.size());

        HashSet<String> set = new HashSet<String>();
        set.add("x");
        set.add("y");
        set.add("x");
        System.out.println(set.size());
        System.out.println(set.contains("x"));

        int[] nums = {5, 3, 8, 1, 9};
        Arrays.sort(nums);
        System.out.println(nums[0]);
        System.out.println(nums[4]);
        int[] copy = Arrays.copyOf(nums, 3);
        System.out.println(copy.length);
        System.out.println(copy[2]);
        Arrays.fill(nums, 7);
        System.out.println(nums[0]);

        int[] src = {1, 2, 3, 4};
        int[] dst = new int[4];
        System.arraycopy(src, 1, dst, 0, 2);
        System.out.println(dst[0]);
        System.out.println(dst[1]);
        System.out.println("done-colls");
    }
}
