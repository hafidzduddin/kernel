From aad7e7b8003936a6514d9ae1e9305d00e66df7d0 Mon Sep 17 00:00:00 2001
From: hafidzduddin <hafidzduddin@gmail.com>
Date: Sat, 24 Nov 2012 02:59:14 +0700
Subject: [PATCH] power-supply:Limit the duration of psy_changed wakelocks

---
 kernel/drivers/power/power_supply_core.c |    4 ++--
 1 file changed, 2 insertions(+), 2 deletions(-)

diff --git a/kernel/drivers/power/power_supply_core.c b/kernel/drivers/power/power_supply_core.c
index 1276171..edbd3e0 100644
--- a/kernel/drivers/power/power_supply_core.c
+++ b/kernel/drivers/power/power_supply_core.c
@@ -60,7 +60,7 @@ static void power_supply_changed_work(struct work_struct *work)
 		kobject_uevent(&psy->dev->kobj, KOBJ_CHANGE);
 		spin_lock_irqsave(&psy->changed_lock, flags);
 	}
-	if (!psy->changed)
+	if (!psy->changed && wake_lock_active(&psy->work_wake_lock))
 		wake_unlock(&psy->work_wake_lock);
 	spin_unlock_irqrestore(&psy->changed_lock, flags);
 }
@@ -73,7 +73,7 @@ void power_supply_changed(struct power_supply *psy)
 
 	spin_lock_irqsave(&psy->changed_lock, flags);
 	psy->changed = true;
-	wake_lock(&psy->work_wake_lock);
+	wake_lock_timeout(&psy->work_wake_lock, HZ * 2);
 	spin_unlock_irqrestore(&psy->changed_lock, flags);
 	schedule_work(&psy->changed_work);
 }
-- 
1.7.10


