# Sweep round 3: sites most affected by the #fff / image-cover fixes
$urls = @(
  "https://www.qq.com/", "https://www.sina.com.cn/", "https://www.163.com/",
  "https://www.jd.com/", "https://www.taobao.com/", "https://www.sohu.com/",
  "https://www.ifeng.com/", "https://www.ctrip.com/",
  "https://music.163.com/", "https://leetcode.cn/",
  "https://www.bilibili.com/", "https://www.baidu.com/",
  "https://www.deepseek.com/"
)
$i = 0
foreach ($u in $urls) {
  $i++
  $name = ($u -replace 'https://','' -replace 'www\.','' -replace '[^a-zA-Z0-9]','_')
  Write-Output "[$i/$($urls.Count)] $u"
  & D:\mycppos1\nefuOS\tools\_bloxd_shot.ps1 -Url $u -DelayMs 30000 -KillAfterSec 46 -Tag "sw3_${i}_$name" | Out-Null
  Write-Output "  done -> sw3_${i}_$name"
}
Write-Output "SWEEP3 COMPLETE"
