# diag/ — 접속 종료 원인 확인용 교체 스크립트

원본(decompiled_ida_all) 본문 그대로 + 맨 위에 `___MOD.log(...)` 한 줄. 게임 동작은 바뀌지 않는다.

rules.txt에 추가:
```
MeramNetworkService.ClientOnDisconnect|@diag/ClientOnDisconnect.lua
MeramNetworkService.ClientAccountBanned|@diag/ClientAccountBanned.lua
```
`___MOD.log` 출력이 어디로 가는지(Player.log인지, 다른 곳인지)는 본인 월드에서 먼저 확인할 것.
