const TrebuchetModel = (function () {
    let scene, camera, renderer, controls;
    let trebuchetGroup, armGroup, projectileMesh, trajectoryLine;
    let isAnimating = false;
    let animationTime = 0;
    let currentArmAngle = 0;
    let targetArmAngle = 0;
    let onFireComplete = null;
    let container, canvas;

    function init(canvasId) {
        canvas = document.getElementById(canvasId);
        container = canvas.parentElement;

        scene = new THREE.Scene();
        scene.background = new THREE.Color(0x0a0e1a);
        scene.fog = new THREE.Fog(0x0a0e1a, 50, 200);

        const rect = container.getBoundingClientRect();
        camera = new THREE.PerspectiveCamera(50, rect.width / rect.height, 0.1, 1000);
        camera.position.set(12, 8, 15);
        camera.lookAt(0, 2, 0);

        renderer = new THREE.WebGLRenderer({ canvas: canvas, antialias: true, alpha: true });
        renderer.setSize(rect.width, rect.height);
        renderer.setPixelRatio(window.devicePixelRatio);
        renderer.shadowMap.enabled = true;
        renderer.shadowMap.type = THREE.PCFSoftShadowMap;

        setupLights();
        setupGround();
        createTrebuchet();
        setupControls();

        window.addEventListener('resize', onWindowResize);
        animate();
    }

    function setupLights() {
        const ambient = new THREE.AmbientLight(0x404060, 0.5);
        scene.add(ambient);

        const sunLight = new THREE.DirectionalLight(0xfff0d4, 1.2);
        sunLight.position.set(15, 25, 10);
        sunLight.castShadow = true;
        sunLight.shadow.mapSize.width = 2048;
        sunLight.shadow.mapSize.height = 2048;
        sunLight.shadow.camera.left = -30;
        sunLight.shadow.camera.right = 30;
        sunLight.shadow.camera.top = 30;
        sunLight.shadow.camera.bottom = -30;
        sunLight.shadow.camera.near = 0.5;
        sunLight.shadow.camera.far = 100;
        scene.add(sunLight);

        const fillLight = new THREE.DirectionalLight(0x4466aa, 0.3);
        fillLight.position.set(-10, 10, -10);
        scene.add(fillLight);

        const hemiLight = new THREE.HemisphereLight(0x87ceeb, 0x362d1f, 0.3);
        scene.add(hemiLight);
    }

    function setupGround() {
        const groundGeo = new THREE.PlaneGeometry(100, 100);
        const groundMat = new THREE.MeshStandardMaterial({
            color: 0x2d3d2a,
            roughness: 0.9,
            metalness: 0.0
        });
        const ground = new THREE.Mesh(groundGeo, groundMat);
        ground.rotation.x = -Math.PI / 2;
        ground.receiveShadow = true;
        scene.add(ground);

        const gridHelper = new THREE.GridHelper(60, 60, 0x3a4a6a, 0x253048);
        gridHelper.position.y = 0.01;
        scene.add(gridHelper);
    }

    function createWoodMaterial(dark = false) {
        return new THREE.MeshStandardMaterial({
            color: dark ? 0x3d2817 : 0x5c3d24,
            roughness: 0.75,
            metalness: 0.05
        });
    }

    function createMetalMaterial() {
        return new THREE.MeshStandardMaterial({
            color: 0x4a4a4a,
            roughness: 0.35,
            metalness: 0.85
        });
    }

    function createTrebuchet() {
        trebuchetGroup = new THREE.Group();
        scene.add(trebuchetGroup);

        createBaseFrame();
        createAFrame();
        createArm();
        createCounterweight();
        createSpring();
        createProjectile();
    }

    function createBaseFrame() {
        const woodMat = createWoodMaterial();
        const darkWoodMat = createWoodMaterial(true);

        const beamGroup = new THREE.Group();
        const beam1 = new THREE.Mesh(new THREE.BoxGeometry(10, 0.4, 0.5), woodMat);
        beam1.position.set(0, 0.2, -2);
        beam1.castShadow = true;
        beam1.receiveShadow = true;
        beamGroup.add(beam1);

        const beam2 = new THREE.Mesh(new THREE.BoxGeometry(10, 0.4, 0.5), woodMat);
        beam2.position.set(0, 0.2, 2);
        beam2.castShadow = true;
        beam2.receiveShadow = true;
        beamGroup.add(beam2);

        for (let i = -3; i <= 3; i += 2) {
            const cross = new THREE.Mesh(new THREE.BoxGeometry(0.3, 0.3, 4.5), woodMat);
            cross.position.set(i, 0.2, 0);
            cross.castShadow = true;
            cross.receiveShadow = true;
            beamGroup.add(cross);
        }

        trebuchetGroup.add(beamGroup);

        const wheelMat = createWoodMaterial(true);
        const wheelGeo = new THREE.CylinderGeometry(0.7, 0.7, 0.3, 20);
        const wheelPositions = [
            [-4, 0.7, -2], [4, 0.7, -2], [-4, 0.7, 2], [4, 0.7, 2]
        ];
        wheelPositions.forEach(pos => {
            const wheel = new THREE.Mesh(wheelGeo, wheelMat);
            wheel.rotation.z = Math.PI / 2;
            wheel.position.set(pos[0], pos[1], pos[2]);
            wheel.castShadow = true;
            trebuchetGroup.add(wheel);

            const hub = new THREE.Mesh(new THREE.CylinderGeometry(0.15, 0.15, 0.35, 12), createMetalMaterial());
            hub.rotation.z = Math.PI / 2;
            hub.position.copy(wheel.position);
            trebuchetGroup.add(hub);
        });
    }

    function createAFrame() {
        const woodMat = createWoodMaterial();
        const poleGroup = new THREE.Group();

        const poleGeo = new THREE.CylinderGeometry(0.18, 0.22, 7, 12);

        const leftPole1 = new THREE.Mesh(poleGeo, woodMat);
        leftPole1.position.set(-2.5, 3.5, -1);
        leftPole1.rotation.z = -0.15;
        leftPole1.castShadow = true;
        poleGroup.add(leftPole1);

        const leftPole2 = new THREE.Mesh(poleGeo, woodMat);
        leftPole2.position.set(-2.5, 3.5, 1);
        leftPole2.rotation.z = -0.15;
        leftPole2.castShadow = true;
        poleGroup.add(leftPole2);

        const rightPole1 = new THREE.Mesh(poleGeo, woodMat);
        rightPole1.position.set(2.5, 3.5, -1);
        rightPole1.rotation.z = 0.15;
        rightPole1.castShadow = true;
        poleGroup.add(rightPole1);

        const rightPole2 = new THREE.Mesh(poleGeo, woodMat);
        rightPole2.position.set(2.5, 3.5, 1);
        rightPole2.rotation.z = 0.15;
        rightPole2.castShadow = true;
        poleGroup.add(rightPole2);

        const topBeam = new THREE.Mesh(new THREE.BoxGeometry(5.5, 0.4, 0.4), woodMat);
        topBeam.position.set(0, 6.8, 0);
        topBeam.castShadow = true;
        poleGroup.add(topBeam);

        const cross1 = new THREE.Mesh(new THREE.BoxGeometry(0.2, 4, 0.2), woodMat);
        cross1.position.set(0, 5, -1);
        cross1.rotation.z = 0.3;
        cross1.castShadow = true;
        poleGroup.add(cross1);

        const cross2 = new THREE.Mesh(new THREE.BoxGeometry(0.2, 4, 0.2), woodMat);
        cross2.position.set(0, 5, 1);
        cross2.rotation.z = -0.3;
        cross2.castShadow = true;
        poleGroup.add(cross2);

        trebuchetGroup.add(poleGroup);
    }

    function createArm() {
        armGroup = new THREE.Group();
        armGroup.position.set(0, 6.8, 0);
        trebuchetGroup.add(armGroup);

        const woodMat = createWoodMaterial();
        const arm = new THREE.Mesh(new THREE.BoxGeometry(9, 0.5, 0.5), woodMat);
        arm.position.set(1.5, 0, 0);
        arm.castShadow = true;
        armGroup.add(arm);

        const reinforcement = new THREE.Mesh(new THREE.BoxGeometry(3, 0.6, 0.7), createWoodMaterial(true));
        reinforcement.position.set(-2, 0, 0);
        reinforcement.castShadow = true;
        armGroup.add(reinforcement);

        const slingGeo = new THREE.ConeGeometry(0.35, 1.2, 8);
        const slingMat = new THREE.MeshStandardMaterial({
            color: 0x6b4423,
            roughness: 0.9,
            side: THREE.DoubleSide
        });
        const sling = new THREE.Mesh(slingGeo, slingMat);
        sling.position.set(6, -0.5, 0);
        sling.rotation.z = Math.PI;
        sling.castShadow = true;
        armGroup.add(sling);

        const ropeMat = new THREE.LineBasicMaterial({ color: 0x8b7355 });
        for (let i = 0; i < 4; i++) {
            const angle = (i / 4) * Math.PI * 2;
            const points = [];
            points.push(new THREE.Vector3(5.8 + Math.cos(angle) * 0.3, -0.2, Math.sin(angle) * 0.3));
            points.push(new THREE.Vector3(6.2 + Math.cos(angle) * 0.15, -1.0, Math.sin(angle) * 0.15));
            const ropeGeo = new THREE.BufferGeometry().setFromPoints(points);
            armGroup.add(new THREE.Line(ropeGeo, ropeMat));
        }
    }

    function createCounterweight() {
        const counterweightGroup = new THREE.Group();
        counterweightGroup.position.set(0, 6.8, 0);
        trebuchetGroup.add(counterweightGroup);

        const metalMat = new THREE.MeshStandardMaterial({
            color: 0x2a2a2a,
            roughness: 0.5,
            metalness: 0.6
        });
        const weightBox = new THREE.Mesh(new THREE.BoxGeometry(1.5, 2, 1.5), metalMat);
        weightBox.position.set(-3.5, -1.5, 0);
        weightBox.castShadow = true;
        counterweightGroup.add(weightBox);

        for (let i = 0; i < 2; i++) {
            for (let j = 0; j < 2; j++) {
                const ropePoints = [];
                ropePoints.push(new THREE.Vector3(-3 + i * 0.6 - 0.3, 0, j * 0.4 - 0.2));
                ropePoints.push(new THREE.Vector3(-3 + i * 0.6 - 0.3, -1, j * 0.4 - 0.2));
                const ropeGeo = new THREE.BufferGeometry().setFromPoints(ropePoints);
                const ropeMat = new THREE.LineBasicMaterial({ color: 0x8b7355 });
                counterweightGroup.add(new THREE.Line(ropeGeo, ropeMat));
            }
        }
    }

    function createSpring() {
        const springGroup = new THREE.Group();
        springGroup.position.set(0, 6.8, 0);
        springGroup.name = "springGroup";
        armGroup.add(springGroup);

        const turns = 8;
        const height = 1.2;
        const radius = 0.4;
        const points = [];
        const segments = turns * 32;

        for (let i = 0; i <= segments; i++) {
            const t = i / segments;
            const angle = t * turns * Math.PI * 2;
            points.push(new THREE.Vector3(
                Math.cos(angle) * radius,
                -t * height + height / 2,
                Math.sin(angle) * radius
            ));
        }

        const curve = new THREE.CatmullRomCurve3(points);
        const tubeGeo = new THREE.TubeGeometry(curve, segments * 2, 0.06, 8, false);
        const springMat = new THREE.MeshStandardMaterial({
            color: 0x888888,
            roughness: 0.3,
            metalness: 0.85,
            emissive: 0x111111
        });
        const springMesh = new THREE.Mesh(tubeGeo, springMat);
        springMesh.rotation.y = Math.PI / 2;
        springMesh.castShadow = true;
        springGroup.add(springMesh);

        const axleGeo = new THREE.CylinderGeometry(0.15, 0.15, 1.5, 16);
        const axle = new THREE.Mesh(axleGeo, createMetalMaterial());
        axle.rotation.x = Math.PI / 2;
        axle.castShadow = true;
        springGroup.add(axle);
    }

    function createProjectile() {
        const rockMat = new THREE.MeshStandardMaterial({
            color: 0x6b6b6b,
            roughness: 0.95,
            metalness: 0.0
        });
        projectileMesh = new THREE.Mesh(
            new THREE.SphereGeometry(0.35, 20, 20),
            rockMat
        );
        projectileMesh.castShadow = true;
        projectileMesh.visible = true;
        scene.add(projectileMesh);
        resetProjectile();

        trajectoryLine = null;
    }

    function resetProjectile() {
        const worldPos = new THREE.Vector3();
        const dummy = new THREE.Object3D();
        dummy.position.set(6, -0.5, 0);
        armGroup.updateMatrixWorld(true);
        dummy.applyMatrix4(armGroup.matrixWorld);
        projectileMesh.position.copy(dummy.position);
        projectileMesh.visible = true;
    }

    function setupControls() {
        let isDragging = false;
        let prevMouse = { x: 0, y: 0 };
        let spherical = { theta: Math.PI / 4, phi: Math.PI / 3, radius: 20 };
        const target = new THREE.Vector3(0, 3, 0);

        canvas.addEventListener('mousedown', (e) => {
            isDragging = true;
            prevMouse = { x: e.clientX, y: e.clientY };
        });

        canvas.addEventListener('mouseup', () => { isDragging = false; });
        canvas.addEventListener('mouseleave', () => { isDragging = false; });

        canvas.addEventListener('mousemove', (e) => {
            if (!isDragging) return;
            const dx = e.clientX - prevMouse.x;
            const dy = e.clientY - prevMouse.y;
            spherical.theta -= dx * 0.005;
            spherical.phi = Math.max(0.1, Math.min(Math.PI / 2 - 0.1, spherical.phi + dy * 0.005));
            prevMouse = { x: e.clientX, y: e.clientY };
            updateCamera();
        });

        canvas.addEventListener('wheel', (e) => {
            e.preventDefault();
            spherical.radius = Math.max(8, Math.min(50, spherical.radius + e.deltaY * 0.02));
            updateCamera();
        });

        function updateCamera() {
            camera.position.x = target.x + spherical.radius * Math.sin(spherical.phi) * Math.cos(spherical.theta);
            camera.position.y = target.y + spherical.radius * Math.cos(spherical.phi);
            camera.position.z = target.z + spherical.radius * Math.sin(spherical.phi) * Math.sin(spherical.theta);
            camera.lookAt(target);
        }

        updateCamera();
    }

    function onWindowResize() {
        if (!container) return;
        const rect = container.getBoundingClientRect();
        camera.aspect = rect.width / rect.height;
        camera.updateProjectionMatrix();
        renderer.setSize(rect.width, rect.height);
    }

    function setArmAngle(angleRad) {
        if (armGroup) {
            armGroup.rotation.z = angleRad;
            currentArmAngle = angleRad;
        }
    }

    function loadProjectile() {
        resetProjectile();
        if (trajectoryLine) {
            scene.remove(trajectoryLine);
            trajectoryLine = null;
        }
    }

    function fireProjectile(velocity, angleDeg, mass = 10, onComplete) {
        if (isAnimating) return;
        isAnimating = true;
        onFireComplete = onComplete;
        animationTime = 0;

        const worldPos = new THREE.Vector3();
        const dummy = new THREE.Object3D();
        dummy.position.set(6, -0.5, 0);
        armGroup.updateMatrixWorld(true);
        dummy.applyMatrix4(armGroup.matrixWorld);
        projectileMesh.position.copy(dummy.position);
        projectileMesh.visible = true;

        const theta = TrebuchetPhysics.degToRad(angleDeg);
        projectileMesh.userData = {
            vx: velocity * Math.cos(theta),
            vy: velocity * Math.sin(theta),
            startX: dummy.position.x,
            startY: dummy.position.y,
            startZ: dummy.position.z,
            drag: 0.5 * TrebuchetPhysics.AIR_DENSITY * 0.47 * 0.0314 / mass,
            trajectoryPoints: [new THREE.Vector3(dummy.position.x, dummy.position.y, dummy.position.z)]
        };

        targetArmAngle = Math.PI / 3;
        currentArmAngle = -Math.PI / 4;
        armGroup.rotation.z = currentArmAngle;
    }

    function showTrajectory(points, color = 0xffb347) {
        if (trajectoryLine) {
            scene.remove(trajectoryLine);
        }
        const geometry = new THREE.BufferGeometry().setFromPoints(points);
        const material = new THREE.LineBasicMaterial({ color, linewidth: 2 });
        trajectoryLine = new THREE.Line(geometry, material);
        scene.add(trajectoryLine);
    }

    function animate() {
        requestAnimationFrame(animate);

        if (isAnimating && projectileMesh && projectileMesh.userData.vx !== undefined) {
            animationTime += 0.016;

            if (animationTime < 0.3) {
                const t = animationTime / 0.3;
                armGroup.rotation.z = currentArmAngle + (targetArmAngle - currentArmAngle) * Math.min(1, t * 2);
            } else {
                const dt = 0.016;
                const data = projectileMesh.userData;
                const vx = data.vx;
                const vy = data.vy;
                const vMag = Math.sqrt(vx * vx + vy * vy);
                const ax = -data.drag * vMag * vx;
                const ay = -TrebuchetPhysics.GRAVITY - data.drag * vMag * vy;
                data.vx += ax * dt;
                data.vy += ay * dt;
                projectileMesh.position.x += data.vx * dt;
                projectileMesh.position.y += data.vy * dt;

                if (data.trajectoryPoints.length < 500) {
                    data.trajectoryPoints.push(new THREE.Vector3(
                        projectileMesh.position.x,
                        projectileMesh.position.y,
                        projectileMesh.position.z
                    ));
                    showTrajectory(data.trajectoryPoints, 0xff6b35);
                }

                projectileMesh.rotation.x += 0.1;
                projectileMesh.rotation.z += 0.15;

                if (projectileMesh.position.y <= 0.1) {
                    projectileMesh.position.y = 0.1;
                    isAnimating = false;
                    if (onFireComplete) onFireComplete();
                }
            }
        }

        const springGroup = scene.getObjectByName("springGroup", true);
        if (springGroup) {
            springGroup.rotation.z = Math.sin(Date.now() * 0.001) * 0.05;
        }

        renderer.render(scene, camera);
    }

    function reset() {
        isAnimating = false;
        setArmAngle(-Math.PI / 6);
        loadProjectile();
        if (trajectoryLine) {
            scene.remove(trajectoryLine);
            trajectoryLine = null;
        }
    }

    function isFiring() { return isAnimating; }

    return {
        init,
        setArmAngle,
        fireProjectile,
        reset,
        resetProjectile,
        isFiring
    };
})();
