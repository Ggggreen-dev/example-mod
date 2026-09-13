/**
 * NOCLIP HUD & LIMITER MOD - Archivo Principal
 * 
 * Este mod proporciona un sistema de Noclip con HUD visual que muestra:
 * - Estado del Noclip (ON/OFF)
 * - Contador de muertes en modo Noclip
 * - Precisión (porcentaje de frames sin colisión)
 * 
 * El mod incluye límites configurables para muertes y precisión que 
 * fuerzan la pérdida si se alcanzan.
 */

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

using namespace geode::prelude;

/**
 * ============================================================================
 * MODIFICACIÓN DE PlayLayer - INICIALIZACIÓN E INTERFAZ DEL HUD
 * ============================================================================
 * 
 * Este struct modifica la clase PlayLayer para:
 * 1. Inicializar las variables del mod cuando comienza un nivel
 * 2. Crear etiquetas de HUD para mostrar información en tiempo real
 * 3. Leer configuraciones del usuario desde Geode
 */
class $modify(NoclipPlayLayer, PlayLayer) {
	
	// ========================================================================
	// CAMPOS PERSONALIZADOS (Fields)
	// ========================================================================
	
	/// Flag que indica si el noclip está activo en este intento
	bool m_noclipEnabled = true;
	
	/// Contador de muertes "invisibles" mientras noclip está activo
	int m_noclipDeaths = 0;
	
	/// Total de frames transcurridos durante el intento
	float m_totalFrames = 0.0f;
	
	/// Frames donde el jugador estuvo colisionando con obstáculos
	float m_collisionFrames = 0.0f;
	
	/// Flag para detectar cambios de estado de colisión entre frames
	bool m_isCollidingThisFrame = false;
	
	// Etiquetas del HUD para mostrar información en tiempo real
	CCLabelBMFont* m_noclipStatusLabel = nullptr;
	CCLabelBMFont* m_deathCountLabel = nullptr;
	CCLabelBMFont* m_accuracyLabel = nullptr;
	
	// Límites configurados por el usuario (se cargan en init)
	int m_deathLimit = 5;
	float m_accuracyLimit = 90.0f;
	
	/**
	 * ========================================================================
	 * HOOK: PlayLayer::init()
	 * ========================================================================
	 * 
	 * Intercepta la inicialización del nivel para:
	 * - Resetear variables del mod
	 * - Cargar configuraciones del usuario
	 * - Crear etiquetas de HUD
	 * - Posicionar elementos visuales en la pantalla
	 */
	bool init(GJGameLevel* level, bool p1, bool p2) {
		// Llamar a la función original de PlayLayer::init
		if (!PlayLayer::init(level, p1, p2)) {
			return false;
		}
		
		log::debug("Noclip HUD & Limiter: Inicializando nivel");
		
		// ====================================================================
		// RESETEAR VARIABLES DEL MOD
		// ====================================================================
		m_noclipEnabled = true;
		m_noclipDeaths = 0;
		m_totalFrames = 0.0f;
		m_collisionFrames = 0.0f;
		m_isCollidingThisFrame = false;
		
		// ====================================================================
		// CARGAR CONFIGURACIONES DEL USUARIO
		// ====================================================================
		
		// Obtener el valor de "death-limit" desde las configuraciones de Geode
		// Si no existe, usar el valor por defecto de 5
		auto deathLimitValue = Mod::get()->getSettingValue<int64_t>("death-limit");
		m_deathLimit = static_cast<int>(deathLimitValue);
		
		// Obtener el valor de "accuracy-limit" desde las configuraciones de Geode
		// Si no existe, usar el valor por defecto de 90.0
		auto accuracyLimitValue = Mod::get()->getSettingValue<double>("accuracy-limit");
		m_accuracyLimit = static_cast<float>(accuracyLimitValue);
		
		log::debug("Noclip HUD & Limiter: Límite de muertes = {}", m_deathLimit);
		log::debug("Noclip HUD & Limiter: Límite de precisión = {:.2f}%", m_accuracyLimit);
		
		// ====================================================================
		// CREAR ETIQUETAS DEL HUD
		// ====================================================================
		
		// Obtener la capa de interfaz del juego para añadir elementos visuales
		auto gameLayer = this->m_gameLayer;
		if (!gameLayer) {
			log::warn("Noclip HUD & Limiter: No se pudo obtener gameLayer");
			return true;
		}
		
		// ETIQUETA 1: Estado del Noclip (ON/OFF)
		m_noclipStatusLabel = CCLabelBMFont::create("Noclip: ON", "bigFont.fnt");
		m_noclipStatusLabel->setPosition({60.0f, 280.0f});
		m_noclipStatusLabel->setScale(0.6f);
		m_noclipStatusLabel->setColor({0, 255, 0}); // Verde
		m_noclipStatusLabel->setZOrder(1000);
		gameLayer->addChild(m_noclipStatusLabel);
		log::debug("Noclip HUD & Limiter: Etiqueta de estado creada");
		
		// ETIQUETA 2: Contador de muertes con límite
		m_deathCountLabel = CCLabelBMFont::create(
			fmt::format("Deaths: 0 / {}", m_deathLimit).c_str(),
			"bigFont.fnt"
		);
		m_deathCountLabel->setPosition({60.0f, 250.0f});
		m_deathCountLabel->setScale(0.6f);
		m_deathCountLabel->setColor({255, 255, 255}); // Blanco
		m_deathCountLabel->setZOrder(1000);
		gameLayer->addChild(m_deathCountLabel);
		log::debug("Noclip HUD & Limiter: Etiqueta de muertes creada");
		
		// ETIQUETA 3: Precisión actual con límite
		m_accuracyLabel = CCLabelBMFont::create(
			fmt::format("Accuracy: 100.00% / {:.1f}%", m_accuracyLimit).c_str(),
			"bigFont.fnt"
		);
		m_accuracyLabel->setPosition({60.0f, 220.0f});
		m_accuracyLabel->setScale(0.6f);
		m_accuracyLabel->setColor({255, 255, 0}); // Amarillo
		m_accuracyLabel->setZOrder(1000);
		gameLayer->addChild(m_accuracyLabel);
		log::debug("Noclip HUD & Limiter: Etiqueta de precisión creada");
		
		return true;
	}
	
	/**
	 * ========================================================================
	 * HOOK: PlayLayer::update(float dt)
	 * ========================================================================
	 * 
	 * Intercepta cada frame del juego para:
	 * - Incrementar contadores de frames
	 * - Detectar colisiones del jugador
	 * - Calcular precisión en tiempo real
	 * - Actualizar etiquetas del HUD
	 * - Verificar si se alcanzó el límite de precisión
	 */
	void update(float dt) {
		// Llamar a la función original de PlayLayer::update
		PlayLayer::update(dt);
		
		// Solo actualizar si el juego está en curso (no pausado, no ganado, no perdido)
		if (!this->m_player1 || this->m_isPaused || this->m_levelComplete) {
			return;
		}
		
		// ====================================================================
		// INCREMENTAR CONTADORES DE FRAMES
		// ====================================================================
		m_totalFrames += 1.0f;
		
		// ====================================================================
		// DETECTAR COLISIONES DEL JUGADOR
		// ====================================================================
		
		// Verificar si el jugador está colisionando en este frame
		// m_player1->m_collidedObjects contiene objetos que han colisionado este frame
		bool isCollidingNow = this->m_player1->m_collidedObjects->count() > 0;
		
		if (isCollidingNow) {
			// Si hay colisión en este frame, incrementar contador de frames de colisión
			m_collisionFrames += 1.0f;
		}
		
		m_isCollidingThisFrame = isCollidingNow;
		
		// ====================================================================
		// CALCULAR PRECISIÓN ACTUAL
		// ====================================================================
		
		float currentAccuracy = 100.0f;
		if (m_totalFrames > 0.0f) {
			// Precisión = (frames sin colisión / total de frames) * 100
			currentAccuracy = ((m_totalFrames - m_collisionFrames) / m_totalFrames) * 100.0f;
		}
		
		// ====================================================================
		// ACTUALIZAR ETIQUETAS DEL HUD
		// ====================================================================
		
		if (m_deathCountLabel) {
			m_deathCountLabel->setString(
				fmt::format("Deaths: {} / {}", m_noclipDeaths, m_deathLimit).c_str()
			);
			
			// Cambiar color a rojo si se acerca al límite de muertes
			if (m_noclipDeaths >= m_deathLimit) {
				m_deathCountLabel->setColor({255, 0, 0}); // Rojo
			} else {
				m_deathCountLabel->setColor({255, 255, 255}); // Blanco
			}
		}
		
		if (m_accuracyLabel) {
			m_accuracyLabel->setString(
				fmt::format("Accuracy: {:.2f}% / {:.1f}%", currentAccuracy, m_accuracyLimit).c_str()
			);
			
			// Cambiar color a rojo si la precisión está por debajo del límite
			if (currentAccuracy < m_accuracyLimit) {
				m_accuracyLabel->setColor({255, 0, 0}); // Rojo
			} else {
				m_accuracyLabel->setColor({255, 255, 0}); // Amarillo
			}
		}
		
		// ====================================================================
		// VERIFICAR LÍMITE DE PRECISIÓN
		// ====================================================================
		
		// Si la precisión cae por debajo del límite y noclip está activo,
		// desactivar noclip y forzar la muerte del jugador
		if (m_noclipEnabled && currentAccuracy < m_accuracyLimit) {
			log::info("Noclip HUD & Limiter: Precisión {:.2f}% por debajo del límite {:.1f}%", 
					  currentAccuracy, m_accuracyLimit);
			
			// Desactivar noclip
			m_noclipEnabled = false;
			if (m_noclipStatusLabel) {
				m_noclipStatusLabel->setString("Noclip: OFF");
				m_noclipStatusLabel->setColor({255, 0, 0}); // Rojo
			}
			
			// Forzar la muerte del jugador llamando a la función de muerte
			this->destroyPlayer(this->m_player1, nullptr);
		}
	}
};

/**
 * ============================================================================
 * MODIFICACIÓN DE PlayerObject - INTERCEPCIÓN DE MUERTE
 * ============================================================================
 * 
 * Este struct modifica la clase PlayerObject para:
 * 1. Interceptar cuando el jugador toca un obstáculo
 * 2. Si noclip está activo, evitar la muerte inmediata
 * 3. Contar las "muertes invisibles"
 * 4. Verificar si se alcanzó el límite de muertes
 */
class $modify(NoclipPlayerObject, PlayerObject) {
	
	/// Flag para detectar si es la primera vez que colisiona en este choque
	bool m_firstCollisionThisImpact = true;
	
	/**
	 * ========================================================================
	 * HOOK: PlayerObject::pushPlayer(int pushID)
	 * ========================================================================
	 * 
	 * Esta función es llamada cuando el jugador toca un obstáculo.
	 * Interceptamos esta función para:
	 * - Detectar el choque
	 * - Contar muertes invisibles si noclip está activo
	 * - Permitir la muerte real si se alcanzó el límite
	 */
	void pushPlayer(int pushID) {
		// Obtener la referencia al PlayLayer actual
		auto playLayer = NoclipPlayLayer::get();
		if (!playLayer) {
			// Si no existe NoclipPlayLayer, ejecutar comportamiento normal
			PlayerObject::pushPlayer(pushID);
			return;
		}
		
		// ====================================================================
		// VERIFICAR SI NOCLIP ESTÁ ACTIVO
		// ====================================================================
		
		if (playLayer->m_noclipEnabled) {
			log::debug("Noclip HUD & Limiter: Choque interceptado con noclip activo");
			
			// Incrementar el contador de muertes invisibles
			playLayer->m_noclipDeaths += 1;
			log::info("Noclip HUD & Limiter: Muerte invisible #{}", playLayer->m_noclipDeaths);
			
			// ================================================================
			// VERIFICAR SI SE ALCANZÓ EL LÍMITE DE MUERTES
			// ================================================================
			
			if (playLayer->m_noclipDeaths >= playLayer->m_deathLimit) {
				log::info("Noclip HUD & Limiter: Límite de muertes alcanzado ({}/{})", 
						  playLayer->m_noclipDeaths, playLayer->m_deathLimit);
				
				// Desactivar noclip y permitir la muerte real
				playLayer->m_noclipEnabled = false;
				if (playLayer->m_noclipStatusLabel) {
					playLayer->m_noclipStatusLabel->setString("Noclip: OFF");
					playLayer->m_noclipStatusLabel->setColor({255, 0, 0}); // Rojo
				}
				
				// Ejecutar la función original de muerte
				PlayerObject::pushPlayer(pushID);
			} else {
				// Noclip sigue activo, no ejecutar la muerte
				log::debug("Noclip HUD & Limiter: Muerte evitada ({}/{})", 
						   playLayer->m_noclipDeaths, playLayer->m_deathLimit);
				
				// No llamar a PlayerObject::pushPlayer para evitar la muerte
				// El jugador continúa como si nada hubiera pasado
			}
		} else {
			// Noclip no está activo, comportamiento normal
			PlayerObject::pushPlayer(pushID);
		}
	}
};

