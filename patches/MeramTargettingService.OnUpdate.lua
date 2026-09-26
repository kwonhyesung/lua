-- 원본 동작(self:OnDraw())은 그대로 유지하고, F1로 켠 "따라가기"가 활성화된 동안
-- 빨탭(Tab-Tab으로 Fixed된) 대상 쪽으로 이 게임의 최대 입력 속도(1초 5틱 = 200ms 간격)에 맞춰
-- 이동 명령을 보낸다.
-- On/Off 상태는 UIMeramMainHud.HandleKeyDownEvent(F1)와 ___MOD._G.__LuahookFollow로 공유한다
-- (서로 다른 청크라 self로 상태 공유 불가 → Lua 전역 테이블을 브리지로 사용).
local MOVE_INTERVAL = 0.2  -- 1초 5틱
local KEEP_DISTANCE = 2  -- 대상과 유지할 최소 거리(맨해튼)
local elapsed = 0
return function(self, delta)
  self:OnDraw()

  local state = ___MOD._G.__LuahookFollow
  if not state or not state.enabled then
    elapsed = 0
    return
  end

  if not self:IsTargetAvailable() then
    state.enabled = false
    local gameHud = ___MOD._MeramHudService:GetCurrentGameHud()
    if gameHud then
      gameHud:SystemMessage("[따라가기] 대상을 잃어 중지합니다.")
    end
    return
  end

  local target = self:GetTarget()
  local entity = target and target:GetEntity()
  if not entity or not ___MOD.isvalid(entity) then
    return
  end

  ___MOD.pcall(function()
    local localPlayer = ___MOD._UserService and ___MOD._UserService.LocalPlayer
    if not localPlayer then
      return
    end
    local playerEntity = localPlayer:GetChildByName("Player")
    if not playerEntity then
      return
    end

    local myMov = playerEntity.MeramMovementComponent
    local targetMov = entity.MeramMovementComponent
    if not myMov or not targetMov then
      return
    end

    local myPos = myMov.Position
    local targetPos = targetMov.Position
    if not myPos or not targetPos then
      return
    end

    -- 맨해튼 거리: KEEP_DISTANCE 이내면 정지 (그 거리를 유지)
    local dx = targetPos.x - myPos.x
    local dy = targetPos.y - myPos.y
    if ___MOD.math.abs(dx) + ___MOD.math.abs(dy) <= KEEP_DISTANCE then
      return
    end

    -- 이동 쓰로틀: 1초 5틱(200ms)로 제한 (매 프레임 보내면 서버가 비정상 입력으로 보고 끊음)
    elapsed = elapsed + (delta or 0)
    if elapsed < MOVE_INTERVAL then
      return
    end
    elapsed = elapsed - MOVE_INTERVAL

    -- 이 게임은 4방향(상하좌우) 그리드 이동만 지원 (대각선 없음, MeramDirection.OnBeginPlay의
    -- DELTA 테이블 기준 NORTH=(0,-1)/EAST=(1,0)/SOUTH=(0,1)/WEST=(-1,0)). 더 벌어진 축을 우선한다.
    local direction
    if ___MOD.math.abs(dx) >= ___MOD.math.abs(dy) then
      direction = dx > 0 and ___MOD._MeramDirection.EAST or ___MOD._MeramDirection.WEST
    else
      direction = dy > 0 and ___MOD._MeramDirection.SOUTH or ___MOD._MeramDirection.NORTH
    end

    -- 장애물 회피: 게임 자체 이동(MeramPlayerController.Move)이 쓰는 것과 동일한 CanGo 체크로
    -- 직진이 막혀 있으면(벽 등) 좌/우/반대 순으로 갈 수 있는 방향을 찾는다
    -- (MeramMonsterContext.Follow 몬스터 AI가 쓰는 것과 같은 순서).
    local function doorStateGetter(pos)
      return ___MOD._MeramDoorStateService:IsDoorClosed(pos, nil)
    end
    local function canGo(dir)
      return ___MOD._MeramTransformUtils:CanGo(myMov.MapId, myPos, dir, doorStateGetter)
    end

    local candidates = {
      direction,
      ___MOD._MeramDirection:GetLeftDirection(direction),
      ___MOD._MeramDirection:GetRightDirection(direction),
      ___MOD._MeramDirection:GetOppositeDirection(direction),
    }

    local controller = localPlayer.MeramPlayerController
    if not controller then
      return
    end
    for _, dir in ___MOD.ipairs(candidates) do
      if canGo(dir) then
        controller:TryMove(dir, false)
        break
      end
    end
  end)
end
