# Sweep N large sites, screenshot + log each into verify_out/sweep_<n>_<name>.*
$urls = @(
  "https://www.wikipedia.org/", "https://zh.wikipedia.org/",
  "https://news.ycombinator.com/", "https://www.bing.com/",
  "https://duckduckgo.com/", "https://www.bilibili.com/",
  "https://www.zhihu.com/", "https://www.qq.com/",
  "https://www.sina.com.cn/", "https://www.163.com/",
  "https://www.jd.com/", "https://www.taobao.com/",
  "https://www.sohu.com/", "https://www.ifeng.com/",
  "https://www.csdn.net/", "https://www.douban.com/",
  "https://www.runoob.com/", "https://juejin.cn/",
  "https://segmentfault.com/", "https://leetcode.cn/",
  "https://music.163.com/", "https://www.ctrip.com/"
)
$i = 0
foreach ($u in $urls) {
  $i++
  $name = ($u -replace 'https://','' -replace 'www\.','' -replace '[^a-zA-Z0-9]','_')
  Write-Output "[$i/$($urls.Count)] $u"
  & D:\mycppos1\nefuOS\tools\_bloxd_shot.ps1 -Url $u -DelayMs 16000 -KillAfterSec 28 -Tag "sweep_${i}_$name" | Out-Null
  Write-Output "  done -> sweep_${i}_$name"
}
Write-Output "SWEEP COMPLETE"
