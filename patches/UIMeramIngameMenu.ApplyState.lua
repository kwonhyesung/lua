-- self(C# 바인딩 객체)는 없던 필드를 동적으로 못 만들어서, 토글 상태는
-- self가 아니라 이 청크의 클로저 지역변수에 둔다 (이 청크는 세션에 딱 한 번만 로드됨).
local groups = {
  { id = "hunt", title = "사냥·이동", items = {
      { id = "auto_hunt", name = "자동사냥", value = false },
      { id = "skill_1", name = "1번 스킬 5칸", value = false },
      { id = "auto_cave", name = "자동굴이동", value = false },
      { id = "auto_loot", name = "자동줍기", value = false },
  }},
  { id = "heal", title = "회복", items = {
      { id = "auto_potion", name = "자동물약 50%", value = false },
      { id = "auto_revive", name = "자동부활", value = false },
      { id = "mp_boost", name = "공력증강 10%", value = false },
      { id = "phoenix_heal", name = "봉황의기원 10%", value = false },
      { id = "nuri_heal", name = "누리의기원 10%", value = false },
      { id = "life_heal", name = "생명의기원 20%", value = false },
      { id = "spirit_heal", name = "신령의기원 20%", value = false },
  }},
  { id = "buff", title = "전투·버프", items = {
      { id = "honma", name = "혼마술", value = false },
      { id = "white_tiger", name = "백호의희원", value = false },
      { id = "geumgang", name = "금강불체", value = false },
      { id = "paryeok", name = "파력무참", value = false },
      { id = "protect", name = "보호(본인+빨탭)", value = false },
      { id = "armor", name = "무장(본인+빨탭)", value = false },
  }},
}

-- (group, item) 평면 리스트로 미리 펼쳐서 번호를 매긴다.
local flat = {}
for _, group in ___MOD.ipairs(groups) do
  for _, item in ___MOD.ipairs(group.items) do
    flat[#flat + 1] = { group = group, item = item }
  end
end
local cursor = 0

-- 안정성 최우선: 새 엔티티는 트리거 버튼 1개뿐이고, 위치/크기는 전혀 건드리지 않는다.
-- 클릭할 때마다 다음 항목으로 넘어가며 기존 확인창(다이얼로그)으로만 상태를 보여준다.
local function ensureTriggerSpawned(self)
  local parent = self.MenuButtonLayout
  if parent:GetChildByName("InjTriggerBtn") ~= nil then
    return
  end
  local trigger = self.MembershipButton:Clone("InjTriggerBtn")
  trigger.Enable = true
  trigger:ConnectEvent(___MOD.ButtonClickEvent, function()
    cursor = cursor % #flat + 1
    local entry = flat[cursor]
    entry.item.value = not entry.item.value
    local state = entry.item.value and "ON" or "OFF"
    ___MOD._MeramUIDialogService:ShowMessageDialog(
      cursor .. "/" .. #flat .. "  " .. entry.group.title .. " > " .. entry.item.name .. ": " .. state,
      nil, nil, nil)
  end)
end

return function(self)
  if self.IsOpen then
    self.Entity.SpriteGUIRendererComponent.ImageRUID = self.RightArrowImageRUID
    self.MenuButtonLayout.Enable = true

    local ok, err = ___MOD.pcall(function()
      ensureTriggerSpawned(self)
    end)
    if not ok then
      ___MOD._MeramUIDialogService:ShowMessageDialog("TRIGGER_FAIL: " .. ___MOD.tostring(err), nil, nil, nil)
    end
  else
    self.Entity.SpriteGUIRendererComponent.ImageRUID = self.LeftArrowImageRUID
    self.MenuButtonLayout.Enable = false
  end
end
