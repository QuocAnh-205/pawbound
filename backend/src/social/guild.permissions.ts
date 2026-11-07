// Ma trận quyền bang hội — NGUỒN DUY NHẤT, kiểm tra ở server. Client chỉ dùng để ẩn/hiện nút (GET /guilds/mine trả permissions).
export type GuildRole = 'OWNER' | 'OFFICER' | 'MEMBER';
export type GuildAction =
  | 'view_requests' // xem danh sách xin vào / lời mời
  | 'invite' // mời người chơi
  | 'review_join' // duyệt/từ chối đơn xin vào
  | 'edit_profile' // đổi mô tả, emblem
  | 'rename' // đổi tên bang
  | 'kick' // kick thành viên cấp THẤP hơn
  | 'promote' // MEMBER -> OFFICER
  | 'demote' // OFFICER -> MEMBER
  | 'transfer' // chuyển quyền chủ bang
  | 'disband' // giải tán
  | 'leave'; // rời bang (chủ bang phải chuyển quyền hoặc giải tán trước)

export const GUILD_PERMISSIONS: Readonly<Record<GuildAction, readonly GuildRole[]>> = {
  view_requests: ['OWNER', 'OFFICER'],
  invite: ['OWNER', 'OFFICER'],
  review_join: ['OWNER', 'OFFICER'],
  edit_profile: ['OWNER', 'OFFICER'],
  rename: ['OWNER'],
  kick: ['OWNER', 'OFFICER'],
  promote: ['OWNER'],
  demote: ['OWNER'],
  transfer: ['OWNER'],
  disband: ['OWNER'],
  leave: ['OFFICER', 'MEMBER'],
};

export const ROLE_RANK: Readonly<Record<GuildRole, number>> = { OWNER: 3, OFFICER: 2, MEMBER: 1 };

// Hành động nhắm vào thành viên khác: người làm phải có quyền VÀ cấp cao hơn hẳn người bị tác động.
// promote chỉ áp cho MEMBER, demote chỉ cho OFFICER, transfer cho bất kỳ ai khác mình.
export function canAct(actor: GuildRole, action: GuildAction, target?: GuildRole): boolean {
  if (!GUILD_PERMISSIONS[action].includes(actor)) return false;
  if (target === undefined) return !['kick', 'promote', 'demote', 'transfer'].includes(action);
  switch (action) {
    case 'kick': return ROLE_RANK[actor] > ROLE_RANK[target];
    case 'promote': return target === 'MEMBER';
    case 'demote': return target === 'OFFICER';
    case 'transfer': return target !== 'OWNER';
    default: return false;  // hành động không nhắm người khác mà lại có target -> từ chối
  }
}

export function permissionsOf(role: GuildRole): GuildAction[] {
  return (Object.keys(GUILD_PERMISSIONS) as GuildAction[]).filter((a) => GUILD_PERMISSIONS[a].includes(role));
}
