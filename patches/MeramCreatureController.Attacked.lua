-- 원본 동작은 그대로 두고, 피격당한 대상(나 자신 포함, 몬스터/다른 플레이어 포함)의 hp/maxHp를
-- NetObjId 기준으로 ___MOD._G 브리지에 캐싱해둔다. 대상 HP는 이 방법 말고는(로컬 플레이어 자신의
-- HP처럼 UI 갱신 이벤트로) 클라이언트에서 읽을 방법이 없어서, 실제로 값이 지나가는 이 지점에서 가로챈다.
-- (MeramTargettingService.OnUpdate의 "따라가기 대상 회복" 로직이 이 캐시를 읽는다)
return function(self, hp, maxHp)
  local netObjId = self.NetObjId
  if netObjId then
    local bridge = ___MOD._G.__LuahookFollow
    if not bridge then
      bridge = {}
      ___MOD._G.__LuahookFollow = bridge
    end
    local creatureHp = bridge.creatureHp
    if not creatureHp then
      creatureHp = {}
      bridge.creatureHp = creatureHp
    end
    creatureHp[netObjId] = { hp = hp, maxHp = maxHp }
  end

  local appearance = self.Entity.MeramAppearanceComponent
  if ___MOD.isvalid(appearance) then
    appearance:OverrideMaterial("material://8287331c-3d8f-4c76-94d6-2c44db0c058d")
    ___MOD._TimerService:SetTimerOnce(function()
      appearance:OverrideMaterial(nil)
    end, 0.1)
  end
  if not ___MOD.isvalid(self.HpPanel) then
    local hpPanelPos = ___MOD.Vector3.up * 1.5
    if ___MOD.isvalid(self.Entity.MeramObjectRenderer) then
      local renderer = self.Entity.MeramObjectRenderer
      if ___MOD.isvalid(renderer) then
        local rect = renderer:GetRect()
        hpPanelPos = ___MOD.Vector3(0, rect.top * 0.01 + 0.3, 0)
      end
    end
    self.HpPanel = ___MOD._MeramHudService:GetHpBar(hpPanelPos, self.Entity)
    if self._T.ObjectType == self._T.OBJECT_PLAYER then
      local playerIdentificationComponent = self.Entity.MeramPlayerIdentificationComponent
      if ___MOD.isvalid(playerIdentificationComponent) then
        playerIdentificationComponent:RefreshPlayerIdendificationEntityPosByHpPanel(hpPanelPos)
      end
    end
  end
  if 0 < hp then
    self._T.TimerId = ___MOD._MeramHudService:SetHpBar(self.HpPanel, hp, maxHp, self._T.TimerId)
  end
  ___MOD._MeramHudService:RefreshObjectProp(self.Entity, ___MOD._MeramHudService.ESyncPropMaxHp, maxHp)
  ___MOD._MeramHudService:RefreshObjectProp(self.Entity, ___MOD._MeramHudService.ESyncPropHp, hp)
end
